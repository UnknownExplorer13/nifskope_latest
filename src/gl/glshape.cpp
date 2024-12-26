/***** BEGIN LICENSE BLOCK *****

BSD License

Copyright (c) 2005-2015, NIF File Format Library and Tools
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. The name of the NIF File Format Library and Tools project may not be
   used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

***** END LICENCE BLOCK *****/

#include "glshape.h"

#include "gl/controllers.h"
#include "gl/glscene.h"
#include "model/nifmodel.h"
#include "io/material.h"
#include "gl/renderer.h"
#include "glview.h"

#include <QDebug>
#include <QElapsedTimer>

Shape::Shape( Scene * s, const QModelIndex & b ) : Node( s, b )
{
	shapeNumber = s->shapes.count();
}

void Shape::clear()
{
	Node::clear();

	clearHash();

	resetSkinning();
	resetVertexData();
	resetSkeletonData();

	sortedTriangles.clear();

	bssp = nullptr;
	bslsp = nullptr;
	bsesp = nullptr;
	alphaProperty = nullptr;

	isLOD = false;
	isDoubleSided = false;
}

void Shape::transform()
{
	if ( needUpdateData ) {
		needUpdateData = false;

		auto nif = NifModel::fromValidIndex( iBlock );
		if ( nif ) {
			clearHash();
			needUpdateBounds = true; // Force update bounds
			updateData(nif);
		} else {
			clear();
			return;
		}
	}

	Node::transform();
}

void Shape::updateBoneTransforms()
{
	if ( !partitions.isEmpty() ) {
		// TODO: implement partitions
		transformRigid = true;
		return;
	}

	qsizetype	numBones = boneData.size();
	if ( numBones < 1 ) {
		transformRigid = true;
		return;
	}
	boneTransforms.fill( 0.0f, numBones * 12 );
	transformRigid = false;

	Node * root = findParent( skeletonRoot );

	for ( qsizetype i = 0; i < numBones; i++ ) {
		const BoneData &	bw = boneData.at( i );
		Node * bone = root ? root->findChild( bw.bone ) : nullptr;
		Transform	t = bw.trans;
		if ( bone )
			t = skeletonTrans * bone->localTrans( skeletonRoot ) * t;
		else
			t = skeletonTrans * t;
		float *	bt = boneTransforms.data() + ( i * 12 );
		bt[0] = t.rotation.data()[0] * t.scale;
		bt[1] = t.rotation.data()[3] * t.scale;
		bt[2] = t.rotation.data()[6] * t.scale;
		bt[3] = t.rotation.data()[1] * t.scale;
		bt[4] = t.rotation.data()[4] * t.scale;
		bt[5] = t.rotation.data()[7] * t.scale;
		bt[6] = t.rotation.data()[2] * t.scale;
		bt[7] = t.rotation.data()[5] * t.scale;
		bt[8] = t.rotation.data()[8] * t.scale;
		bt[9] = t.translation[0];
		bt[10] = t.translation[1];
		bt[11] = t.translation[2];
	}

	qsizetype	numVerts = verts.size();
	transVerts.resize( numVerts );
	transVerts.fill( Vector3() );

	for ( qsizetype i = 0; i < numVerts; i++ ) {
		FloatVector4	v = FloatVector4::convertVector3( &( verts.at( i )[0] ) );
		v[3] = 1.0f;
		const float *	wp = &( boneWeights0.at( i )[0] );
		FloatVector4	vTmp( 0.0f );
		float	wSum = 0.0f;
		for ( int j = 0; j < 8; j++, wp++ ) {
			if ( j == 4 )
				wp = &( boneWeights1.at( i )[0] );
			float	w = *wp;
			if ( w > 0.0f ) {
				int	b = int( w );
				if ( b < 0 || b >= numBones ) [[unlikely]]
					continue;
				w -= float( b );
				const float *	bt = boneTransforms.constData() + ( b * 12 );
				FloatVector4	tmp = FloatVector4::convertVector3( bt ) * v[0];
				tmp += FloatVector4::convertVector3( bt + 3 ) * v[1];
				tmp += FloatVector4::convertVector3( bt + 6 ) * v[2];
				tmp += FloatVector4::convertVector3( bt + 9 ) * v[3];
				vTmp += tmp * w;
				wSum += w;
			}
		}
		if ( wSum > 0.0f )
			v = vTmp / wSum;
		v.convertToVector3( &( transVerts[i][0] ) );
	}

	boundSphere = BoundSphere( transVerts );
	boundSphere.applyInv( worldTrans() );
	needUpdateBounds = false;
}

void Shape::removeInvalidIndices()
{
	qsizetype	numVerts = verts.size();
	qsizetype	numTriangles = sortedTriangles.size();
	// validate triangles' vertex indices, throw out triangles with the wrong ones
	for ( qsizetype i = 0; i < numTriangles; i++ ) {
		const Triangle &	t = sortedTriangles.at( i );
		qsizetype	maxVertex = std::max( t[0], std::max( t[1], t[2] ) );
		if ( maxVertex < numVerts ) [[likely]]
			continue;
		auto	minVertex = std::min( t[0], std::min( t[1], t[2] ) );
		if ( qsizetype( minVertex ) >= numVerts )
			minVertex = 0;
		Triangle &	tmp = sortedTriangles[i];
		if ( qsizetype( tmp[0] ) >= numVerts )
			tmp[0] = minVertex;
		if ( qsizetype( tmp[1] ) >= numVerts )
			tmp[1] = minVertex;
		if ( qsizetype( tmp[2] ) >= numVerts )
			tmp[2] = minVertex;
	}
}

void Shape::drawVerts( float pointSize, int vertexSelected ) const
{
	auto	context = scene->renderer;
	if ( !context )
		return;
	auto	prog = context->useProgram( "selection.prog" );
	if ( !prog )
		return;
	setUniforms( prog );

	int	selectionFlags = 0x0002;
	int	selectionParam = vertexSelected;

	glPointSize( pointSize );

	if ( scene->selecting ) {
		selectionFlags = selectionFlags | 0x0001;
		selectionParam = shapeNumber << 16;
		glDisable( GL_BLEND );
	} else {
		glNormalColor();
		selectionFlags = selectionFlags | ( roundFloat( std::min( std::max( pointSize * 8.0f, 0.0f ), 255.0f ) ) << 8 );
		if ( vertexSelected >= 0 )
			prog->uni4f( "highlightColor", FloatVector4( Color4( cfg.highlight ) ) );
		glEnable( GL_BLEND );
		context->fn->glBlendFuncSeparate( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA );
	}
	prog->uni1i( "selectionFlags", selectionFlags );
	prog->uni1i( "selectionParam", selectionParam );

	qsizetype	numVerts = verts.size();
	context->fn->glDrawArrays( GL_POINTS, 0, GLsizei( numVerts ) );
	if ( !scene->selecting && vertexSelected >= 0 && vertexSelected < numVerts ) {
		pointSize = GLView::Settings::vertexPointSizeSelected;
		glPointSize( pointSize );
		selectionFlags = ( roundFloat( std::min( std::max( pointSize * 8.0f, 0.0f ), 255.0f ) ) << 8 ) | 0x0002;
		prog->uni1i( "selectionFlags", selectionFlags );
		context->fn->glDrawArrays( GL_POINTS, GLint( vertexSelected ), 1 );
	}

	prog->uni1i( "selectionFlags", 0 );
}

void Shape::drawNormals( int btnMask, int vertexSelected, float lineLength ) const
{
	// TODO: implement this
	(void) btnMask;
	(void) vertexSelected;
	(void) lineLength;
}

void Shape::setController( const NifModel * nif, const QModelIndex & iController )
{
	QString contrName = nif->itemName(iController);
	if ( contrName == "NiGeomMorpherController" ) {
		Controller * ctrl = new MorphController( this, iController );
		registerController(nif, ctrl);
	} else if ( contrName == "NiUVController" ) {
		Controller * ctrl = new UVController( this, iController );
		registerController(nif, ctrl);
	} else {
		Node::setController( nif, iController );
	}
}

void Shape::updateImpl( const NifModel * nif, const QModelIndex & index )
{
	Node::updateImpl( nif, index );

	if ( index == iBlock ) {
		shader = nullptr;	// Reset stored shader so it can reassess conditions

		bslsp = nullptr;
		bsesp = nullptr;
		bssp = properties.get<BSShaderLightingProperty>();
		if ( bssp ) {
			auto shaderType = bssp->typeId();
			if ( shaderType == "BSLightingShaderProperty" )
				bslsp = bssp->cast<BSLightingShaderProperty>();
			else if ( shaderType == "BSEffectShaderProperty" )
				bsesp = bssp->cast<BSEffectShaderProperty>();
		}

		alphaProperty = properties.get<AlphaProperty>();

		needUpdateData = true;
		updateShader();

	} else if ( isSkinned && (index == iSkin || index == iSkinData || index == iSkinPart) ) {
		needUpdateData = true;

	} else if ( (bssp && bssp->isParamBlock(index)) || (alphaProperty && index == alphaProperty->index()) ) {
		updateShader();

	}
}

void Shape::boneSphere( const NifModel * nif, const QModelIndex & index ) const
{
	Node * root = findParent( 0 );
	Node * bone = root ? root->findChild( bones.value( index.row() ) ) : 0;
	if ( !bone )
		return;

	Transform boneT = Transform( nif, index );
	Transform t = scene->hasOption(Scene::DoSkinning) ? viewTrans() : Transform();
	t = t * skeletonTrans * bone->localTrans( 0 ) * boneT;

	auto bSphere = BoundSphere( nif, index );
	if ( bSphere.radius > 0.0 ) {
		glColor4f( 1, 1, 1, 0.33f );
		auto pos = boneT.rotation.inverted() * (bSphere.center - boneT.translation);
		drawSphereSimple( t * pos, bSphere.radius, 36 );
	}
}

void Shape::resetSkinning()
{
	isSkinned = false;
	iSkin = iSkinData = iSkinPart = QModelIndex();
}

void Shape::resetVertexData()
{
	iData = iTangentData = QModelIndex();

	verts.clear();
	norms.clear();
	colors.clear();
	tangents.clear();
	bitangents.clear();
	coords.clear();
	triangles.clear();
	tristrips.clear();
}

void Shape::resetSkeletonData()
{
	skeletonRoot = 0;
	skeletonTrans = Transform();

	boneTransforms.clear();
	boneWeights0.clear();
	boneWeights1.clear();

	bones.clear();
	boneData.clear();
	partitions.clear();
	transVerts.clear();
}

void Shape::updateShader()
{
	if ( bslsp )
		translucent = (bslsp->alpha < 1.0) || bslsp->hasRefraction;
	else if ( bsesp )
		translucent = (bsesp->getAlpha() < 1.0) && !alphaProperty;
	else
		translucent = false;

	drawInSecondPass = false;
	if ( translucent )
		drawInSecondPass = true;
	else if ( alphaProperty && alphaProperty->hasAlphaBlend() )
		drawInSecondPass = true;
	else if ( bssp ) {
		if ( bssp->bsVersion >= 170 ) {
			const CE2Material *	sfMat = nullptr;
			bssp->getSFMaterial( sfMat, scene->nifModel );
			if ( sfMat && ( sfMat->shaderRoute != 0 || (sfMat->flags & CE2Material::Flag_IsDecal) ) )
				drawInSecondPass = true;
		} else {
			Material * mat = bssp->getMaterial();
			if ( mat && (mat->hasAlphaBlend() || mat->hasDecal()) )
				drawInSecondPass = true;
		}
	}

	if ( bssp ) {
		depthTest = bssp->depthTest;
		depthWrite = bssp->depthWrite;
		isDoubleSided = bssp->isDoubleSided;
		isVertexAlphaAnimation = bssp->isVertexAlphaAnimation;
	} else {
		depthTest = true;
		depthWrite = true;
		isDoubleSided = false;
		isVertexAlphaAnimation = false;
	}
}

void Shape::setUniforms( NifSkopeOpenGLContext::Program * prog ) const
{
	if ( !prog ) [[unlikely]]
		return;

	const Transform &	t = viewTrans();
	const Transform *	v = &( scene->view );
	const Transform *	m = &t;
	unsigned int	nifVersion = 0;
	if ( scene->nifModel ) [[likely]]
		nifVersion = scene->nifModel->getBSVersion();

	qsizetype	numBones = 0;
	if ( nifVersion < 170 ) {
		// TODO: Starfield skinning is not implemented
		if ( !transformRigid )
			numBones = std::min< qsizetype >( boneTransforms.size() / 12, 100 );
		prog->uni1i( "numBones", int( numBones ) );
	}

	if ( numBones > 0 ) {
		int	l = prog->uniLocation( "boneTransforms" );
		if ( l >= 0 )
			prog->f->glUniformMatrix4x3fv( l, GLsizei( numBones ), GL_FALSE, boneTransforms.constData() );
		m = v;
	}

	if ( nifVersion < 130 && prog->name == "sk_msn.prog" ) [[unlikely]]
		v = &t;
	prog->uni3m( "viewMatrix", v->rotation );
	prog->uni3m( "normalMatrix", m->rotation );
	prog->uni4m( "modelViewMatrix", m->toMatrix4() );
}

bool Shape::bindShape() const
{
	NifSkopeOpenGLContext *	context = scene->renderer;
	if ( !context ) [[unlikely]]
		return false;

	qsizetype	numVerts = verts.size();
	qsizetype	numTriangles = sortedTriangles.size();
	if ( !( numVerts > 0 && numTriangles > 0 ) ) [[unlikely]]
		return false;

	static const float	defaultAttrs[12] = { 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -1.0f, 0.0f };
	static const float *	defaultAttrPtrs[16] = {
		// position, color, normal, tangent
		&( defaultAttrs[4] ), &( defaultAttrs[0] ), &( defaultAttrs[6] ), &( defaultAttrs[9] ),
		// bitangent, weights0, weights1, coord0
		&( defaultAttrs[3] ), &( defaultAttrs[4] ), &( defaultAttrs[4] ), &( defaultAttrs[4] ),
		// coord1..coord8
		&( defaultAttrs[4] ), &( defaultAttrs[4] ), &( defaultAttrs[4] ), &( defaultAttrs[4] ),
		&( defaultAttrs[4] ), &( defaultAttrs[4] ), &( defaultAttrs[4] ), &( defaultAttrs[4] )
	};
	const float *	vertexAttrs[16];
	std::memcpy( vertexAttrs, defaultAttrPtrs, sizeof( float * ) * 16 );
	std::uint64_t	attrModeMask = 0xAAAAAAAAACCBBBC3ULL;

	vertexAttrs[0] = &( verts.constFirst()[0] );

	if ( colors.size() >= numVerts ) {
		vertexAttrs[1] = &( colors.constFirst()[0] );
		attrModeMask &= ~0x00000080ULL;
	}

	if ( norms.size() >= numVerts ) [[likely]] {
		vertexAttrs[2] = &( norms.constFirst()[0] );
		attrModeMask &= ~0x00000800ULL;
	}
	if ( tangents.size() >= numVerts ) [[likely]] {
		vertexAttrs[3] = &( tangents.constFirst()[0] );
		attrModeMask &= ~0x00008000ULL;
	}
	if ( bitangents.size() >= numVerts ) [[likely]] {
		vertexAttrs[4] = &( bitangents.constFirst()[0] );
		attrModeMask &= ~0x00080000ULL;
	}

	if ( boneWeights0.size() >= numVerts ) [[unlikely]] {
		vertexAttrs[5] = &( boneWeights0.constFirst()[0] );
		attrModeMask &= ~0x00800000ULL;
	}
	if ( boneWeights1.size() >= numVerts ) [[unlikely]] {
		vertexAttrs[6] = &( boneWeights1.constFirst()[0] );
		attrModeMask &= ~0x08000000ULL;
	}

	unsigned char	numUVs = (unsigned char) std::min< qsizetype >( coords.size(), 9 );
	for ( unsigned char i = 0; i < numUVs; i++ ) {
		if ( coords[i].size() >= numVerts ) [[likely]] {
			vertexAttrs[i + 7] = &( coords.at( i ).constFirst()[0] );
			attrModeMask &= ~( 0x80000000ULL << ( i << 2 ) );
		}
	}

	size_t	elementDataSize = size_t( numTriangles ) * sizeof( Triangle );
	if ( !( dataHash.attrMask && numVerts == dataHash.numVerts && elementDataSize == dataHash.elementBytes ) ) {
		dataHash = NifSkopeOpenGLContext::ShapeDataHash( std::uint32_t( numVerts ), attrModeMask, elementDataSize,
															vertexAttrs, sortedTriangles.constData() );
	}

	context->bindShape( dataHash, vertexAttrs, sortedTriangles.constData() );

	return true;
}
