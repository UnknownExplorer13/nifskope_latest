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

#include <QDebug>
#include <QElapsedTimer>

Shape::Shape( Scene * s, const QModelIndex & b ) : Node( s, b )
{
	shapeNumber = s->shapes.count();
}

void Shape::clear()
{
	Node::clear();

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

	clearHash();
}

void Shape::transform()
{
	if ( needUpdateData ) {
		needUpdateData = false;

		auto nif = NifModel::fromValidIndex( iBlock );
		if ( nif ) {
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

	transVerts.resize( numVerts );
	transVerts.fill( Vector3() );

	for ( int i = 0; i < numVerts; i++ ) {
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
		clearHash();
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
	numVerts = 0;

	iData = iTangentData = QModelIndex();

	verts.clear();
	norms.clear();
	colors.clear();
	tangents.clear();
	bitangents.clear();
	coords.clear();
	coords2.clear();
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

void Shape::drawShape( std::uint16_t attrMask, FloatVector4 color ) const
{
	NifSkopeOpenGLContext *	context = scene->renderer;
	if ( !context ) [[unlikely]]
		return;

	qsizetype	numVerts = verts.size();
	qsizetype	numTriangles = sortedTriangles.size();
	if ( !( numVerts > 0 && numTriangles > 0 ) ) [[unlikely]]
		return;

	const float *	vertexAttrs[16];
	FloatVector4	defaultPos( 0.0f );
	FloatVector4	defaultColor( color );
	FloatVector4	defaultNormal( 0.0f, 0.0f, 1.0f, 0.0f );
	FloatVector4	defaultTangent( 0.0f, -1.0f, 0.0f, 0.0f );
	FloatVector4	defaultBitangent( 1.0f, 0.0f, 0.0f, 0.0f );
	std::uint64_t	attrModeMask = 0U;

	if ( attrMask & 0x0001 ) [[likely]] {
		vertexAttrs[0] = &( verts.constFirst()[0] );
		attrModeMask = 3U;
	}

	if ( attrMask & 0x0002 ) [[likely]] {
		if ( coords2.size() >= numVerts ) {
			vertexAttrs[1] = &( coords2.constFirst()[0] );
			attrModeMask = attrModeMask | 0x00000040U;
		} else if ( coords.size() >= 1 && coords.constFirst().size() >= numVerts ) {
			vertexAttrs[1] = &( coords.constFirst().constFirst()[0] );
			attrModeMask = attrModeMask | 0x00000020U;
		} else {
			vertexAttrs[1] = &( defaultPos[0] );
			attrModeMask = attrModeMask | 0x000000C0U;
		}
	}

	if ( ( attrMask & 0x0004 ) && colors.size() >= numVerts && scene->hasOption(Scene::DoVertexColors) ) {
		vertexAttrs[2] = &( colors.constFirst()[0] );
		attrModeMask = attrModeMask | 0x00000400U;
	} else {
		vertexAttrs[2] = &( defaultColor[0] );
		attrModeMask = attrModeMask | 0x00000C00U;
	}

	if ( attrMask & 0x0008 ) [[likely]] {
		if ( norms.size() < numVerts ) [[unlikely]] {
			vertexAttrs[3] = &( defaultNormal[0] );
			attrModeMask = attrModeMask | 0x0000B000U;
		} else {
			vertexAttrs[3] = &( norms.constFirst()[0] );
			attrModeMask = attrModeMask | 0x00003000U;
		}
	}

	if ( attrMask & 0x0010 ) [[likely]] {
		if ( tangents.size() < numVerts ) [[unlikely]] {
			vertexAttrs[4] = &( defaultTangent[0] );
			attrModeMask = attrModeMask | 0x000B0000U;
		} else {
			vertexAttrs[4] = &( tangents.constFirst()[0] );
			attrModeMask = attrModeMask | 0x00030000U;
		}
	}

	if ( attrMask & 0x0020 ) [[likely]] {
		if ( bitangents.size() < numVerts ) [[unlikely]] {
			vertexAttrs[5] = &( defaultBitangent[0] );
			attrModeMask = attrModeMask | 0x00B00000U;
		} else {
			vertexAttrs[5] = &( bitangents.constFirst()[0] );
			attrModeMask = attrModeMask | 0x00300000U;
		}
	}

	if ( ( attrMask & 0x00C0 ) && !transformRigid ) [[unlikely]] {
		if ( attrMask & 0x0040 ) {
			if ( boneWeights0.size() < numVerts ) {
				vertexAttrs[6] = &( defaultPos[0] );
				attrModeMask = attrModeMask | 0x0C000000U;
			} else {
				vertexAttrs[6] = &( boneWeights0.constFirst()[0] );
				attrModeMask = attrModeMask | 0x04000000U;
			}
		}
		if ( attrMask & 0x0080 ) {
			if ( boneWeights1.size() < numVerts ) {
				vertexAttrs[7] = &( defaultPos[0] );
				attrModeMask = attrModeMask | 0xC0000000U;
			} else {
				vertexAttrs[7] = &( boneWeights1.constFirst()[0] );
				attrModeMask = attrModeMask | 0x40000000U;
			}
		}
	}

	if ( attrMask & 0xFF00 ) [[unlikely]] {
		for ( unsigned char i = 8; i < 16; i++ ) {
			if ( attrMask & ( 1U << i ) ) {
				qsizetype	j = i - 8;
				if ( coords.size() > j && coords.at( j ).size() >= numVerts ) {
					vertexAttrs[i] = &( coords.at( j ).constFirst()[0] );
					attrModeMask = attrModeMask | ( 2ULL << ( i << 2 ) );
				} else {
					vertexAttrs[i] = &( defaultPos[0] );
					attrModeMask = attrModeMask | ( 10ULL << ( i << 2 ) );
				}
			}
		}
	}

	if ( !dataHash.attrMask ) {
		dataHash = NifSkopeOpenGLContext::ShapeDataHash(
						std::uint32_t( numVerts ), attrModeMask, size_t( numTriangles ) * sizeof( Triangle ),
						vertexAttrs, sortedTriangles.constData() );
	}

	context->drawShape( dataHash, (unsigned int) ( numTriangles * 3 ),
						GL_TRIANGLES, GL_UNSIGNED_SHORT, vertexAttrs, sortedTriangles.constData() );
}
