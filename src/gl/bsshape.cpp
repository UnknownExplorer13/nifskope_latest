#include "bsshape.h"

#include "gl/glnode.h"
#include "gl/glscene.h"
#include "gl/renderer.h"
#include "io/material.h"
#include "model/nifmodel.h"
#include "glview.h"

void BSShape::updateImpl( const NifModel * nif, const QModelIndex & index )
{
	Shape::updateImpl( nif, index );

	if ( index == iBlock ) {
		isLOD = nif->isNiBlock( iBlock, "BSMeshLODTriShape" );
		if ( isLOD )
			emit nif->lodSliderChanged(true);
	}
}

void BSShape::updateData( const NifModel * nif )
{
	auto vertexFlags = nif->get<BSVertexDesc>(iBlock, "Vertex Desc");

	isDynamic = nif->blockInherits(iBlock, "BSDynamicTriShape");

	hasVertexColors = vertexFlags.HasFlag(VertexAttribute::VA_COLOR);

	dataBound = BoundSphere(nif, iBlock);

	// Is the shape skinned?
	resetSkinning();
	if ( vertexFlags.HasFlag(VertexAttribute::VA_SKINNING) ) {
		isSkinned = true;

		QString skinInstName, skinDataName;
		if ( nif->getBSVersion() >= 130 ) {
			skinInstName = "BSSkin::Instance";
			skinDataName = "BSSkin::BoneData";
		} else {
			skinInstName = "NiSkinInstance";
			skinDataName = "NiSkinData";
		}

		iSkin = nif->getBlockIndex( nif->getLink( nif->getIndex( iBlock, "Skin" ) ), skinInstName );
		if ( iSkin.isValid() ) {
			iSkinData = nif->getBlockIndex( nif->getLink( iSkin, "Data" ), skinDataName );
			if ( nif->getBSVersion() == 100 )
				iSkinPart = nif->getBlockIndex( nif->getLink( iSkin, "Skin Partition" ), "NiSkinPartition" );
		}
	}

	// Fill vertex data
	resetVertexData();
	int numVerts = 0;
	if ( isSkinned && iSkinPart.isValid() ) {
		// For skinned geometry, the vertex data is stored in the NiSkinPartition
		// The triangles are split up among the partitions
		iData = nif->getIndex( iSkinPart, "Vertex Data" );
		int dataSize = nif->get<int>( iSkinPart, "Data Size" );
		int vertexSize = nif->get<int>( iSkinPart, "Vertex Size" );
		if ( iData.isValid() && dataSize > 0 && vertexSize > 0 )
			numVerts = dataSize / vertexSize;
	} else {
		iData = nif->getIndex( iBlock, "Vertex Data" );
		if ( iData.isValid() )
			numVerts = nif->rowCount( iData );
	}

	TexCoords coordset; // For compatibility with coords list

	QVector<Vector4> dynVerts;
	if ( isDynamic ) {
		dynVerts = nif->getArray<Vector4>( iBlock, "Vertices" );
		int nDynVerts = dynVerts.size();
		if ( nDynVerts < numVerts )
			numVerts = nDynVerts;
	}

	for ( int i = 0; i < numVerts; i++ ) {
		auto idx = nif->getIndex( iData, i );
		float bitX;

		if ( isDynamic ) {
			auto& dynv = dynVerts.at(i);
			verts << Vector3( dynv );
			bitX = dynv[3];
		} else {
			verts << nif->get<Vector3>( idx, "Vertex" );
			bitX = nif->get<float>( idx, "Bitangent X" );
		}

		// Bitangent Y/Z
		auto bitY = nif->get<float>( idx, "Bitangent Y" );
		auto bitZ = nif->get<float>( idx, "Bitangent Z" );

		coordset << nif->get<HalfVector2>( idx, "UV" );
		norms += nif->get<ByteVector3>( idx, "Normal" );
		tangents += nif->get<ByteVector3>( idx, "Tangent" );
		bitangents += Vector3( bitX, bitY, bitZ );

		auto vcIdx = nif->getIndex( idx, "Vertex Colors" );
		colors += vcIdx.isValid() ? nif->get<ByteColor4>( vcIdx ) : Color4(0, 0, 0, 1);
	}

	// Add coords as the first set of QList
	coords.append( coordset );

	numVerts = int( verts.size() );

	// Fill triangle data
	if ( isSkinned && iSkinPart.isValid() ) {
		auto iPartitions = nif->getIndex( iSkinPart, "Partitions" );
		if ( iPartitions.isValid() ) {
			int n = nif->rowCount( iPartitions );
			for ( int i = 0; i < n; i++ )
				triangles << nif->getArray<Triangle>( nif->getIndex( iPartitions, i ), "Triangles" );
		}
	} else {
		auto iTriData = nif->getIndex( iBlock, "Triangles" );
		if ( iTriData.isValid() )
			triangles = nif->getArray<Triangle>( iTriData );
	}
	qsizetype	numTriangles = triangles.size();
	if ( isLOD && numTriangles > 0 ) {
		auto lod0 = nif->get<uint>( iBlock, "LOD0 Size" );
		auto lod1 = nif->get<uint>( iBlock, "LOD1 Size" );
		auto lod2 = nif->get<uint>( iBlock, "LOD2 Size" );

		// If Level2, render all
		// If Level1, also render Level0
		numTriangles = 0;
		switch ( scene->lodLevel ) {
		case Scene::Level0:
			numTriangles = qsizetype( lod2 );
			[[fallthrough]];
		case Scene::Level1:
			numTriangles += qsizetype( lod1 );
			[[fallthrough]];
		case Scene::Level2:
		default:
			numTriangles += qsizetype( lod0 );
			break;
		}
	}
	if ( numTriangles >= triangles.size() )
		sortedTriangles = triangles;
	else if ( numTriangles > 0 )
		sortedTriangles = triangles.mid( 0, numTriangles );
	else
		sortedTriangles.clear();
	removeInvalidIndices();

	// Fill skeleton data
	resetSkeletonData();
	if ( isSkinned && iSkin.isValid() ) {
		skeletonRoot = nif->getLink( iSkin, "Skeleton Root" );
		if ( nif->getBSVersion() < 130 )
			skeletonTrans = Transform( nif, iSkinData );

		bones = nif->getLinkArray( iSkin, "Bones" );
		auto nTotalBones = bones.size();

		boneData.fill( BoneData(), nTotalBones );
		for ( int i = 0; i < nTotalBones; i++ )
			boneData[i].bone = bones[i];

		boneWeights0.resize( numVerts );
		boneWeights1.resize( numVerts );

		for ( int i = 0; i < numVerts; i++ ) {
			auto idx = nif->getIndex( iData, i );
			auto wts = nif->getArray<float>( idx, "Bone Weights" );
			auto bns = nif->getArray<quint8>( idx, "Bone Indices" );
			if ( wts.size() < 4 || bns.size() < 4 )
				continue;

			for ( int j = 0; j < 4; j++ ) {
				if ( bns[j] < nTotalBones && wts[j] > 0.0f )
					boneWeights0[i][j] = float( bns[j] ) + ( std::min( wts[j], 1.0f ) * float( 65535.0 / 65536.0 ) );
			}
		}

		auto b = nif->getIndex( iSkinData, "Bone List" );
		for ( int i = 0; i < nTotalBones; i++ )
			boneData[i].setTransform( nif, nif->getIndex( b, i ) );
	}
}

QModelIndex BSShape::vertexAt( int idx ) const
{
	auto nif = NifModel::fromIndex( iBlock );
	if ( !nif )
		return QModelIndex();

	// Vertices are on NiSkinPartition in version 100
	auto blk = iBlock;
	if ( iSkinPart.isValid() ) {
		if ( isDynamic )
			return nif->getIndex( nif->getIndex( blk, "Vertices" ), idx );

		blk = iSkinPart;
	}

	return nif->getIndex( nif->getIndex( nif->getIndex( blk, "Vertex Data" ), idx ), "Vertex" );
}

void BSShape::transformShapes()
{
	if ( isHidden() )
		return;

	auto nif = scene->nifModel;
	if ( !nif ) {
		clear();
		return;
	}

	Node::transformShapes();

	if ( !( isSkinned && scene->hasOption(Scene::DoSkinning) ) ) [[likely]] {
		transformRigid = true;
		return;
	}

	updateBoneTransforms();
}

void BSShape::drawShapes( NodeList * secondPass )
{
	if ( isHidden() )
		return;

	// TODO: Only run this if BSXFlags has "EditorMarkers present" flag
	if ( !scene->hasOption(Scene::ShowMarkers) && name.contains( QLatin1StringView("EditorMarker") ) )
		return;

	// Draw translucent meshes in second pass
	if ( secondPass && drawInSecondPass ) {
		secondPass->add( this );
		return;
	}

	auto nif = NifModel::fromIndex( iBlock );

	if ( !bindShape() )
		return;

	if ( scene->selecting && scene->isSelModeVertex() ) [[unlikely]] {
		glDisable( GL_FRAMEBUFFER_SRGB );
		drawVerts();
		return;
	}

	// Render polygon fill slightly behind alpha transparency and wireframe
	glEnable( GL_POLYGON_OFFSET_FILL );
	if ( drawInSecondPass )
		glPolygonOffset( 0.5f, 1.0f );
	else
		glPolygonOffset( 1.0f, 2.0f );

	auto	context = scene->renderer;
	GLsizei	numIndices = GLsizei( sortedTriangles.size() * 3 );

	if ( !scene->selecting ) [[likely]] {
		if ( nif->getBSVersion() >= 151 )
			glEnable( GL_FRAMEBUFFER_SRGB );
		else
			glDisable( GL_FRAMEBUFFER_SRGB );
		shader = context->setupProgram( this, shader );

	} else {
		if ( nif->getBSVersion() >= 151 )
			glDisable( GL_FRAMEBUFFER_SRGB );

		auto	prog = context->useProgram( "selection.prog" );
		if ( prog ) {
			setUniforms( prog );
			prog->uni1i( "selectionFlags", 0x0001 );
			prog->uni1i( "selectionParam", ( scene->isSelModeObject() ? nodeId : -1 ) );
		}
	}

	context->fn->glDrawElements( GL_TRIANGLES, GLsizei( numIndices ), GL_UNSIGNED_SHORT, (void *) 0 );

	context->stopProgram();

	glDisable( GL_POLYGON_OFFSET_FILL );
}

void BSShape::drawVerts() const
{
	int	vertexSelected = -1;

	if ( !scene->selecting ) {
		bool	selected = ( iBlock == scene->currentBlock );
		if ( iSkinPart.isValid() ) {
			// Vertices are on NiSkinPartition in version 100
			selected |= ( iSkinPart == scene->currentBlock );
			selected |= isDynamic;
		}
		if ( selected ) {
			// Highlight selected vertex
			auto idx = scene->currentIndex;
			auto n = idx.data( NifSkopeDisplayRole ).toString();
			if ( n == "Vertex" || n == "Vertices" )
				vertexSelected = idx.parent().row();
		}
	}

	Shape::drawVerts( GLView::Settings::vertexSelectPointSize, vertexSelected );
}

void BSShape::drawSelection() const
{
	if ( !scene->isSelModeVertex() ) {
		if ( scene->hasOption(Scene::ShowNodes) )
			Node::drawSelection();

		if ( !scene->isSelModeObject() )
			return;
	}

	auto nif = scene->nifModel;
	if ( isHidden() || !nif )
		return;
	auto idx = scene->currentIndex;
	auto blk = scene->currentBlock;

	// Is the current block extra data
	bool extraData = false;

	// Set current block name and detect if extra data
	auto blockName = nif->itemName( blk );
	// Don't do anything if this block is not the current block
	//	or if there is not extra data
	if ( blockName.startsWith( QLatin1StringView("BSPackedCombined") ) )
		extraData = true;
	else if ( blk != iBlock && blk != iSkin && blk != iSkinData && blk != iSkinPart && !scene->isSelModeVertex() )
		return;

	auto	context = scene->renderer;
	if ( !( context && bindShape() ) )
		return;

	glDepthFunc( GL_LEQUAL );
	glEnable( GL_DEPTH_TEST );
	glDepthMask( GL_FALSE );
	glDisable( GL_CULL_FACE );
	glDisable( GL_FRAMEBUFFER_SRGB );

	if ( scene->isSelModeVertex() ) {
		drawVerts();
		return;
	}

	glEnable( GL_BLEND );
	context->fn->glBlendFuncSeparate( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA );

	// Name of this index
	auto n = idx.data( NifSkopeDisplayRole ).toString();
	// Name of this index's parent
	auto p = idx.parent().data( NifSkopeDisplayRole ).toString();
	// Parent index
	auto pBlock = nif->getBlockIndex( nif->getParent( blk ) );

	GLfloat lineWidth = GLView::Settings::lineWidthWireframe;

	glEnable( GL_POLYGON_OFFSET_FILL );
	glEnable( GL_POLYGON_OFFSET_LINE );
	glEnable( GL_POLYGON_OFFSET_POINT );
	glPolygonOffset( -1.0f, -2.0f );

	float normalScale = bounds().radius / 20;
	normalScale /= 2.0f;

	if ( normalScale < 0.1f )
		normalScale = 0.1f;


	if ( !extraData ) {
		if ( n == "Bounding Sphere" ) {
			auto sph = BoundSphere( nif, idx );
			if ( sph.radius > 0.0f )
				Shape::drawBoundingSphere( sph, FloatVector4( 1.0f, 1.0f, 1.0f, 0.33f ) );
		} else if ( nif->getBSVersion() >= 151 && n == "Bounding Box" ) {
			const NifItem *	boundsItem = nif->getItem( idx );
			Vector3	boundsCenter, boundsDims;
			if ( boundsItem ) {
				boundsCenter = nif->get<Vector3>( boundsItem->child( 0 ) );
				boundsDims = nif->get<Vector3>( boundsItem->child( 1 ) );
			}
			float	minVal = std::min( boundsDims[0], std::min( boundsDims[1], boundsDims[2] ) );
			float	maxVal = std::max( boundsDims[0], std::max( boundsDims[1], boundsDims[2] ) );
			if ( minVal > 0.0f && maxVal < 2.1e9f )
				Shape::drawBoundingBox( boundsCenter, boundsDims, FloatVector4( 1.0f, 1.0f, 1.0f, 0.33f ) );
		}
	}

	if ( blockName.startsWith( QLatin1StringView("BSPackedCombined") ) && pBlock == iBlock ) {
		QVector<QModelIndex> idxs;
		if ( n == "Bounding Sphere" ) {
			idxs += idx;
		} else if ( n.startsWith( QLatin1StringView("BSPackedCombined") ) ) {
			auto data = nif->getIndex( idx, "Object Data" );
			int dataCt = nif->rowCount( data );

			for ( int i = 0; i < dataCt; i++ ) {
				auto d = nif->getIndex( data, i );

				auto c = nif->getIndex( d, "Combined" );
				int cCt = nif->rowCount( c );

				for ( int j = 0; j < cCt; j++ ) {
					idxs += nif->getIndex( nif->getIndex( c, j ), "Bounding Sphere" );
				}
			}
		}

		if ( !idxs.size() ) {
			glPopMatrix();
			return;
		}

#if 0
		Vector3 pTrans = nif->get<Vector3>( nif->getIndex( pBlock, 1 ), "Translation" );
#endif
		auto iBSphere = nif->getIndex( pBlock, "Bounding Sphere" );
		Vector3 pbvC = nif->get<Vector3>( nif->getIndex( iBSphere, 0, 2 ) );
		float pbvR = nif->get<float>( nif->getIndex( iBSphere, 1, 2 ) );

		if ( pbvR > 0.0 ) {
			scene->setGLColor( 0.0f, 1.0f, 0.0f, 0.33f );
			scene->drawSphereSimple( pbvC, pbvR, 72 );
			bindShape();
		}

		glPopMatrix();

		for ( auto i : idxs ) {
			// Transform compound
			auto iTrans = nif->getIndex( i.parent(), 1 );
			Matrix mat = nif->get<Matrix>( iTrans, "Rotation" );
			//auto trans = nif->get<Vector3>( iTrans, "Translation" );
			float scale = nif->get<float>( iTrans, "Scale" );

			Vector3 bvC = nif->get<Vector3>( i, "Center" );
			float bvR = nif->get<float>( i, "Radius" );

			Transform t;
			t.rotation = mat.inverted();
			t.translation = bvC;
			t.scale = scale;

			glPushMatrix();
			glMultMatrix( scene->view * t );

			if ( bvR > 0.0 ) {
				scene->setGLColor( 1.0f, 1.0f, 1.0f, 0.33f );
				scene->drawSphereSimple( Vector3( 0, 0, 0 ), bvR, 72 );
				bindShape();
			}

			glPopMatrix();
		}

		glPushMatrix();
		glMultMatrix( viewTrans() );
	}

	if ( n == "Vertex Data" || n == "Vertex" || n == "Vertices" ) {
		int	s = -1;
		if ( (n == "Vertex Data" && p == "Vertex Data")
			 || (n == "Vertices" && p == "Vertices") ) {
			s = idx.row();
		} else if ( n == "Vertex" ) {
			s = idx.parent().row();
		}

		Shape::drawVerts( GLView::Settings::vertexPointSize, s );
	}

	// Draw Lines lambda
	auto lines = [this, &normalScale, &lineWidth]( const QVector<Vector3> & v ) {
		Shape::drawVerts( GLView::Settings::tbnPointSize, -1 );

		int s = scene->currentIndex.parent().row();
		glBegin( GL_LINES );

		for ( int j = 0; j < verts.size() && j < v.size(); j++ ) {
			glVertex( verts.value( j ) );
			glVertex( verts.value( j ) + v.value( j ) * normalScale * 2 );
			glVertex( verts.value( j ) );
			glVertex( verts.value( j ) - v.value( j ) * normalScale / 2 );
		}

		glEnd();

		if ( s >= 0 ) {
			glDepthFunc( GL_ALWAYS );
			glHighlightColor();
			glLineWidth( GLView::Settings::lineWidthHighlight * 1.2f );
			glBegin( GL_LINES );
			glVertex( verts.value( s ) );
			glVertex( verts.value( s ) + v.value( s ) * normalScale * 2 );
			glVertex( verts.value( s ) );
			glVertex( verts.value( s ) - v.value( s ) * normalScale / 2 );
			glEnd();
			glLineWidth( lineWidth );
		}
	};

	// Draw Normals
	if ( n.contains( QLatin1StringView("Normal") ) ) {
		lines( norms );
	}

	// Draw Tangents
	if ( n.contains( QLatin1StringView("Tangent") ) ) {
		lines( tangents );
	}

	// Draw Triangles
	if ( n == "Triangles" ) {
		int s = -1;
		if ( n == p )
			s = idx.row();
		Shape::drawWireframe( FloatVector4( Color4(cfg.wireframe) ) );
		if ( s >= 0 && s < sortedTriangles.size() )
			Shape::drawTriangles( s, 1, FloatVector4( Color4(cfg.highlight) ) );
	}

	// Draw Segments/Subsegments
	if ( n == "Segment" || n == "Sub Segment" || n == "Num Primitives" ) {
		auto sidx = idx;
		int s;

		QVector<QColor> cols = { { 255, 0, 0, 128 }, { 0, 255, 0, 128 }, { 0, 0, 255, 128 }, { 255, 255, 0, 128 },
								{ 0, 255, 255, 128 }, { 255, 0, 255, 128 }, { 255, 255, 255, 128 }
		};

		glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );

		auto type = idx.sibling( idx.row(), 1 ).data( NifSkopeDisplayRole ).toString();

		bool isSegmentArray = (n == "Segment" && type == "BSGeometrySegmentData" && nif->isArray( idx ));
		bool isSegmentItem = (n == "Segment" && type == "BSGeometrySegmentData" && !nif->isArray( idx ));
		bool isSubSegArray = (n == "Sub Segment" && nif->isArray( idx ));

		int off = 0;
		int cnt = 0;
		int numRec = 0;

		int o = 0;
		if ( isSegmentItem || isSegmentArray ) {
			o = 3; // Offset 3 rows for < 130 BSGeometrySegmentData
		} else if ( isSubSegArray ) {
			o = -3; // Look 3 rows above for Sub Seg Array info
		}

		int maxTris = triangles.size();

		int loopNum = 1;
		if ( isSegmentArray )
			loopNum = nif->rowCount( idx );

		for ( int l = 0; l < loopNum; l++ ) {

			if ( n != "Num Primitives" && !isSubSegArray && !isSegmentArray ) {
				sidx = nif->getIndex( idx, 1 );
			} else if ( isSegmentArray ) {
				sidx = nif->getIndex( nif->getIndex( idx, l ), 1 );
			}
			s = sidx.row() + o;

			off = sidx.sibling( s - 1, 2 ).data().toInt() / 3;
			cnt = sidx.sibling( s, 2 ).data().toInt();
			numRec = sidx.sibling( s + 2, 2 ).data().toInt();

			auto recs = sidx.sibling( s + 3, 0 );
			for ( int i = 0; i < numRec; i++ ) {
				auto subrec = nif->getIndex( recs, i );
				int o = 0;
				if ( subrec.data( NifSkopeDisplayRole ).toString() != "Sub Segment" )
					o = 3; // Offset 3 rows for < 130 BSGeometrySegmentData

				auto suboff = nif->getIndex( subrec, o, 2 ).data().toInt() / 3;
				auto subcnt = nif->getIndex( subrec, o + 1, 2 ).data().toInt();

				for ( int j = suboff; j < subcnt + suboff; j++ ) {
					if ( j >= maxTris )
						continue;

					glColor( Color4( cols.value( i % 7 ) ) );
					Triangle tri = triangles[j];
					glBegin( GL_TRIANGLES );
					glVertex( verts.value( tri.v1() ) );
					glVertex( verts.value( tri.v2() ) );
					glVertex( verts.value( tri.v3() ) );
					glEnd();
				}
			}

			// Sub-segmentless Segments
			if ( numRec == 0 && cnt > 0 ) {
				glColor( Color4( cols.value( (idx.row() + l) % 7 ) ) );

				for ( int i = off; i < cnt + off; i++ ) {
					if ( i >= maxTris )
						continue;

					Triangle tri = triangles[i];
					glBegin( GL_TRIANGLES );
					glVertex( verts.value( tri.v1() ) );
					glVertex( verts.value( tri.v2() ) );
					glVertex( verts.value( tri.v3() ) );
					glEnd();
				}
			}
		}

		return;
	}

	// Draw all bones' bounding spheres
	if ( n == "NiSkinData" || n == "BSSkin::BoneData" ) {
		// Get shape block
		if ( nif->getBlockIndex( nif->getParent( nif->getParent( blk ) ) ) == iBlock ) {
			auto iBones = nif->getIndex( blk, "Bone List" );
			int ct = nif->rowCount( iBones );

			for ( int i = 0; i < ct; i++ ) {
				auto b = nif->getIndex( iBones, i );
				boneSphere( nif, b );
			}
		}
		return;
	}

	// Draw bone bounding sphere
	if ( n == "Bone List" ) {
		if ( nif->isArray( idx ) ) {
			for ( int i = 0; i < nif->rowCount( idx ); i++ )
				boneSphere( nif, nif->getIndex( idx, i ) );
		} else {
			boneSphere( nif, idx );
		}
	}

	// General wireframe
	if ( blk == iBlock && idx != iData && p != "Vertex Data" && p != "Vertices" && n != "Triangles" )
		Shape::drawWireframe( FloatVector4( Color4(cfg.wireframe) ) );

	glDisable( GL_POLYGON_OFFSET_FILL );
	glDisable( GL_POLYGON_OFFSET_LINE );
	glDisable( GL_POLYGON_OFFSET_POINT );
}

BoundSphere BSShape::bounds() const
{
	if ( needUpdateBounds ) {
		needUpdateBounds = false;
		if ( verts.size() ) {
			boundSphere = BoundSphere( verts );
		} else {
			boundSphere = dataBound;
		}
	}

	return worldTrans() * boundSphere;
}
