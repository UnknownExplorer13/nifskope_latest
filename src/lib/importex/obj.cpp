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

#include "message.h"
#include "gl/gltex.h"
#include "gl/glscene.h"
#include "model/nifmodel.h"
#include "nifskope.h"
#include "spells/tangentspace.h"

#include "lib/nvtristripwrapper.h"

#include <QApplication>
#include <QDebug>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QRegularExpression>
#include <QSettings>
#include <QTextStream>

#define tr( x ) QApplication::tr( x )


// "globals"
bool objCulling;
QRegularExpression objCullRegExp;
QStringList expSknMeshWarnings; // Used when notifying user of skinned meshes that were exported statically without weights


/*
 *  .OBJ EXPORT
 */

static void writeData( const NifModel * nif, const QModelIndex & iData, QTextStream & obj, int ofs[1], Transform t )
{
	if ( nif->getBSVersion() < 100 ) {
		// Copy vertices

		QVector<Vector3> verts = nif->getArray<Vector3>( iData, "Vertices" );
		foreach( Vector3 v, verts )
		{
			v = t * v;
			obj << "v " << qSetRealNumberPrecision( 17 ) << v[0] << " " << v[1] << " " << v[2] << "\r\n";
		}

		// Copy texcoords

		QModelIndex iUV = nif->getIndex( iData, "UV Sets" );
		QVector<Vector2> texco = nif->getArray<Vector2>( nif->getIndex( iUV, 0, 0 ) );
		foreach( Vector2 t, texco )
		{
			obj << "vt " << t[0] << " " << 1.0 - t[1] << "\r\n";
		}

		// Copy normals

		QVector<Vector3> norms = nif->getArray<Vector3>( iData, "Normals" );
		foreach( Vector3 n, norms )
		{
			n = t.rotation * n;
			obj << "vn " << n[0] << " " << n[1] << " " << n[2] << "\r\n";
		}

		// Get the triangles

		QVector<Triangle> tris;

		QModelIndex iPoints = nif->getIndex( iData, "Points" );

		if ( iPoints.isValid() ) {
			QVector<QVector<quint16> > strips;

			for ( int r = 0; r < nif->rowCount( iPoints ); r++ )
				strips.append( nif->getArray<quint16>( nif->getIndex( iPoints, r, 0 ) ) );

			tris = triangulate( strips );
		} else {
			tris = nif->getArray<Triangle>( iData, "Triangles" );
		}

		// Write the triangles

		foreach( Triangle t, tris )
		{
			obj << "f";

			for ( int p = 0; p < 3; p++ ) {
				obj << " " << ofs[0] + t[p];

				if ( norms.count() )
					if ( texco.count() )
						obj << "/" << ofs[1] + t[p] << "/" << ofs[2] + t[p];
					else
						obj << "//" << ofs[2] + t[p];


				else if ( texco.count() )
					obj << "/" << ofs[1] + t[p];
			}

			obj << "\r\n";
		}

		ofs[0] += verts.count();
		ofs[1] += texco.count();
		ofs[2] += norms.count();
	} else if ( ( nif->get<BSVertexDesc>( iData, "Vertex Desc" ) & VertexFlags::VF_SKINNED ) && nif->getBSVersion() == 100 ) {
		auto skinID = nif->getLink( nif->getIndex( iData, "Skin" ) );
		auto partID = nif->getLink( nif->getBlockIndex( skinID, "NiSkinInstance" ), "Skin Partition" );
		auto iPartBlock = nif->getBlockIndex( partID, "NiSkinPartition" );
		auto iVertData = nif->getIndex( iPartBlock, "Vertex Data" );
		auto iPartitionData = nif->getIndex( iPartBlock, "Partitions" );

		// Copy vertices, UVs (coords), and normals then write to OBJ
		QVector<Vector3> verts;
		QVector<Vector2> coords;
		QVector<Vector3> norms;

		auto numVerts = nif->get<int>( iPartBlock, "Data Size" ) / nif->get<int>( iPartBlock, "Vertex Size" );
		for ( int i = 0; i < numVerts; i++ ) {
			auto idx = nif->index( i, 0, iVertData );
			verts += nif->get<Vector3>( idx, "Vertex" );
			coords += nif->get<HalfVector2>( idx, "UV" );
			norms += nif->get<ByteVector3>( idx, "Normal" );
		}

		for ( Vector3 & v : verts ) {
			v = t * v;
			obj << "v " << qSetRealNumberPrecision( 17 ) << v[0] << " " << v[1] << " " << v[2] << "\r\n";
		}

		for ( Vector2 & c : coords ) {
			obj << "vt " << c[0] << " " << 1.0 - c[1] << "\r\n";
		}

		for ( Vector3 & n : norms ) {
			n = t.rotation * n;
			obj << "vn " << n[0] << " " << n[1] << " " << n[2] << "\r\n";
		}

		// Copy triangles from all partitions then write to OBJ
		QVector<Triangle> tris;

		auto numPartitions = nif->get<int>( nif->getBlockIndex( skinID ), "Num Partitions" );
		for ( int i = 0; i < numPartitions; i++ ) {
			auto idx = nif->index( i, 0, iPartitionData );
			tris << nif->getArray<Triangle>( idx, "Triangles" );
		}

		for ( const Triangle & t : tris ) {
			obj << "f";

			for ( int p = 0; p < 3; p++ ) {
				obj << " " << ofs[0] + t[p];

				if ( norms.count() )
					if ( coords.count() )
						obj << "/" << ofs[1] + t[p] << "/" << ofs[2] + t[p];
					else
						obj << "//" << ofs[2] + t[p];


				else if ( coords.count() )
					obj << "/" << ofs[1] + t[p];
			}

			obj << "\r\n";
		}

		ofs[0] += verts.count();
		ofs[1] += coords.count();
		ofs[2] += norms.count();
	} else {
		auto iVertData = nif->getIndex( iData, "Vertex Data" );
		if ( !iVertData.isValid() )
			return;

		QVector<Vector3> verts;
		QVector<Vector2> coords;
		QVector<Vector3> norms;
		QVector<ByteColor4> colors;

		auto numVerts = nif->get<int>( iData, "Num Vertices" );
		for ( int i = 0; i < numVerts; i++ ) {
			auto idx = nif->index( i, 0, iVertData );

			verts += nif->get<Vector3>( idx, "Vertex" );
			coords += nif->get<HalfVector2>( idx, "UV" );
			norms += nif->get<ByteVector3>( idx, "Normal" );
			QModelIndex	iColors = nif->getIndex( idx, "Vertex Colors" );
			if ( iColors.isValid() )
				colors += nif->get<ByteColor4>( iColors );
		}

		for ( Vector3 & v : verts ) {
			v = t * v;
			obj << "v " << qSetRealNumberPrecision( 17 ) << v[0] << " " << v[1] << " " << v[2];
			qsizetype	i = qsizetype( &v - verts.constData() );
			if ( i < colors.size() ) {
				ByteColor4	c = colors.at( i );
				obj << qSetRealNumberPrecision( 5 ) << " " << c[0] << " " << c[1] << " " << c[2];
			}
			obj << "\r\n";
		}

		for ( Vector2 & c : coords ) {
			obj << "vt " << c[0] << " " << 1.0 - c[1] << "\r\n";
		}

		for ( Vector3 & n : norms ) {
			n = t.rotation * n;
			obj << "vn " << n[0] << " " << n[1] << " " << n[2] << "\r\n";
		}

		auto tris = nif->getArray<Triangle>( iData, "Triangles" );

		for ( const Triangle & t : tris ) {
			obj << "f";

			for ( int p = 0; p < 3; p++ ) {
				obj << " " << ofs[0] + t[p];

				if ( norms.count() )
					if ( coords.count() )
						obj << "/" << ofs[1] + t[p] << "/" << ofs[2] + t[p];
					else
						obj << "//" << ofs[2] + t[p];


				else if ( coords.count() )
					obj << "/" << ofs[1] + t[p];
			}

			obj << "\r\n";
		}

		ofs[0] += verts.count();
		ofs[1] += coords.count();
		ofs[2] += norms.count();
	}

}

static void writeShape( const NifModel * nif, const QModelIndex & iShape, QTextStream & obj, QTextStream & mtl, int ofs[], Transform t )
{
	QString name = nif->get<QString>( iShape, "Name" );
	QString matn = name, map_Kd, map_Kn, map_Ks, map_Ns, map_d, disp, decal, bump;

	Color3 mata, matd, mats;
	float matt = 1.0, matg = 33.0;

	// export culling
	if ( objCulling && !objCullRegExp.pattern().isEmpty() && nif->get<QString>( iShape, "Name" ).contains( objCullRegExp ) )
		return;

	foreach ( qint32 link, nif->getChildLinks( nif->getBlockNumber( iShape ) ) ) {
		QModelIndex iProp = nif->getBlockIndex( link );

		if ( nif->isNiBlock( iProp, "NiMaterialProperty" ) ) {
			mata = nif->get<Color3>( iProp, "Ambient Color" );
			matd = nif->get<Color3>( iProp, "Diffuse Color" );
			mats = nif->get<Color3>( iProp, "Specular Color" );
			matt = nif->get<float>( iProp, "Alpha" );
			matg = nif->get<float>( iProp, "Glossiness" );
			//matn = nif->get<QString>( iProp, "Name" );
		} else if ( nif->isNiBlock( iProp, "NiTexturingProperty" ) ) {
			QModelIndex iBase = nif->getBlockIndex( nif->getLink( nif->getIndex( iProp, "Base Texture" ), "Source" ), "NiSourceTexture" );
			map_Kd = TexCache::find( nif->get<QString>( iBase, "File Name" ), nif );

			QModelIndex iDark = nif->getBlockIndex( nif->getLink( nif->getIndex( iProp, "Decal Texture 1" ), "Source" ), "NiSourceTexture" );
			decal = TexCache::find( nif->get<QString>( iDark, "File Name" ), nif );

			QModelIndex iBump = nif->getBlockIndex( nif->getLink( nif->getIndex( iProp, "Bump Map Texture" ), "Source" ), "NiSourceTexture" );
			bump = TexCache::find( nif->get<QString>( iBump, "File Name" ), nif );
		} else if ( nif->isNiBlock( iProp, "NiTextureProperty" ) ) {
			QModelIndex iSource = nif->getBlockIndex( nif->getLink( iProp, "Image" ), "NiImage" );
			map_Kd = TexCache::find( nif->get<QString>( iSource, "File Name" ), nif );
		} else if ( ( nif->blockInherits( iProp, "NiSkinInstance" ) ) || ( nif->isNiBlock( iProp, "BSSkin::Instance" ) ) ) {
			// Append shape name to skinned mesh warning list
			expSknMeshWarnings.append( name );
		} else if ( nif->isNiBlock( iProp, { "BSShaderNoLightingProperty", "SkyShaderProperty", "TileShaderProperty" } ) ) {
			map_Kd = TexCache::find( nif->get<QString>( iProp, "File Name" ), nif );
		} else if ( nif->isNiBlock( iProp, "BSEffectShaderProperty" ) ) {
			QModelIndex	iMaterial;
			if ( nif->getBSVersion() >= 151 )
				iMaterial = nif->getIndex( iProp, "Material" );
			if ( iMaterial.isValid() )
				iProp = iMaterial;
			else if ( nif->getBSVersion() >= 151 )
				iProp = nif->getIndex( iProp, "Shader Property Data" );
			map_Kd = TexCache::find( nif->get<QString>( iProp, "Source Texture" ), nif );
		} else if ( nif->isNiBlock( iProp, { "BSShaderPPLightingProperty", "Lighting30ShaderProperty", "BSLightingShaderProperty" } ) ) {
			QModelIndex	iMaterial;
			if ( nif->getBSVersion() >= 151 )
				iMaterial = nif->getIndex( iProp, "Material" );
			if ( iMaterial.isValid() ) {
				iProp = iMaterial;
				map_Kd = TexCache::find( nif->get<QString>( iProp, "Texture 0" ), nif );
				map_Kn = TexCache::find( nif->get<QString>( iProp, "Texture 1" ), nif );
			} else {
				if ( nif->getBSVersion() >= 151 && nif->isNiBlock( iProp, "BSLightingShaderProperty" ) )
					iProp = nif->getIndex( iProp, "Shader Property Data" );
				QModelIndex iArray = nif->getIndex( nif->getBlockIndex( nif->getLink( iProp, "Texture Set" ) ), "Textures" );
				map_Kd = TexCache::find( nif->get<QString>( nif->getIndex( iArray, 0, 0 ) ), nif );
				map_Kn = TexCache::find( nif->get<QString>( nif->getIndex( iArray, 1, 0 ) ), nif );
			}

			auto iSpec = nif->getIndex( iProp, "Specular Color" );
			if ( iSpec.isValid() )
				mats = nif->get<Color3>( iSpec );

			auto iAlpha = nif->getIndex( iProp, "Alpha" );
			if ( iAlpha.isValid() )
				matt = nif->get<float>( iAlpha );

			auto iGlossiness = nif->getIndex( iProp, "Glossiness" );
			if ( iGlossiness.isValid() )
				matg = nif->get<float>( iGlossiness );
		}
	}

	//if ( ! texfn.isEmpty() )
	//	matn += ":" + texfn;

	matn = QString( "Material.%1" ).arg( ofs[0], 6, 16, QChar( '0' ) );

	mtl << "\r\n";
	mtl << "newmtl " << matn << "\r\n";
	mtl << "Ka " << mata[0] << " "  << mata[1] << " "  << mata[2] << "\r\n";
	mtl << "Kd " << matd[0] << " "  << matd[1] << " "  << matd[2] << "\r\n";
	mtl << "Ks " << mats[0] << " "  << mats[1] << " " << mats[2] << "\r\n";
	mtl << "d " << matt << "\r\n";
	mtl << "Ns " << matg << "\r\n";

	if ( !map_Kd.isEmpty() )
		mtl << "map_Kd " << map_Kd << "\r\n";

	if ( !map_Kn.isEmpty() )
		mtl << "map_Kn " << map_Kn << "\r\n";

	if ( !decal.isEmpty() )
		mtl << "decal " << decal << "\r\n";

	if ( !bump.isEmpty() )
		mtl << "bump " << decal << "\r\n";

	obj << "\r\n# " << name << "\r\n\r\ng " << name << "\r\n" << "usemtl " << matn << "\r\n\r\n";

	if ( nif->getBSVersion() < 100 )
		writeData( nif, nif->getBlockIndex( nif->getLink( iShape, "Data" ) ), obj, ofs, t );
	else
		writeData( nif, iShape, obj, ofs, t );
}

static void writeParent( const NifModel * nif, const QModelIndex & iNode, QTextStream & obj, QTextStream & mtl, int ofs[], Transform t )
{
	// export culling
	if ( objCulling && !objCullRegExp.pattern().isEmpty() && nif->get<QString>( iNode, "Name" ).contains( objCullRegExp ) )
		return;

	t = t * Transform( nif, iNode );
	foreach ( int l, nif->getChildLinks( nif->getBlockNumber( iNode ) ) ) {
		QModelIndex iChild = nif->getBlockIndex( l );

		if ( nif->blockInherits( iChild, "NiNode" ) )
			writeParent( nif, iChild, obj, mtl, ofs, t );
		else if ( nif->isNiBlock( iChild, { "NiTriShape", "NiTriStrips" } ) )
			writeShape( nif, iChild, obj, mtl, ofs, t * Transform( nif, iChild ) );
		else if ( nif->isNiBlock( iChild, { "BSTriShape", "BSSubIndexTriShape", "BSMeshLODTriShape" } ) )
			writeShape( nif, iChild, obj, mtl, ofs, t * Transform( nif, iChild ) );
		else if ( nif->blockInherits( iChild, "NiCollisionObject" ) ) {
			QModelIndex iBody = nif->getBlockIndex( nif->getLink( iChild, "Body" ) );

			if ( iBody.isValid() ) {
				Transform bt;
				bt.scale = 7;

				if ( nif->isNiBlock( iBody, "bhkRigidBodyT" ) ) {
					bt.rotation.fromQuat( nif->get<Quat>( iBody, "Rotation" ) );
					bt.translation = Vector3( nif->get<Vector4>( iBody, "Translation" ) * 7 );
				}

				QModelIndex iShape = nif->getBlockIndex( nif->getLink( iBody, "Shape" ) );

				if ( nif->isNiBlock( iShape, "bhkMoppBvTreeShape" ) ) {
					iShape = nif->getBlockIndex( nif->getLink( iShape, "Shape" ) );

					if ( nif->isNiBlock( iShape, "bhkPackedNiTriStripsShape" ) ) {
						QModelIndex iData = nif->getBlockIndex( nif->getLink( iShape, "Data" ) );

						if ( nif->isNiBlock( iData, "hkPackedNiTriStripsData" ) ) {
							bt = t * bt;
							obj << "\r\n# bhkPackedNiTriStripsShape\r\n\r\ng collision\r\n" << "usemtl collision\r\n\r\n";
							QVector<Vector3> verts = nif->getArray<Vector3>( iData, "Vertices" );
							foreach ( Vector3 v, verts ) {
								v = bt * v;
								obj << "v " << v[0] << " " << v[1] << " " << v[2] << "\r\n";
							}

							QModelIndex iTris = nif->getIndex( iData, "Triangles" );

							for ( int t = 0; t < nif->rowCount( iTris ); t++ ) {
								Triangle tri = nif->get<Triangle>( nif->getIndex( iTris, t, 0 ), "Triangle" );
								Vector3 n = nif->get<Vector3>( nif->getIndex( iTris, t, 0 ), "Normal" );

								Vector3 a = verts.value( tri[0] );
								Vector3 b = verts.value( tri[1] );
								Vector3 c = verts.value( tri[2] );

								Vector3 fn = Vector3::crossproduct( b - a, c - a );
								fn.normalize();

								bool flip = Vector3::dotproduct( n, fn ) < 0;

								obj << "f"
								    << " " << tri[0] + ofs[0]
								    << " " << tri[ flip ? 2 : 1 ] + ofs[0]
								    << " " << tri[ flip ? 1 : 2 ] + ofs[0]
								    << "\r\n";
							}

							ofs[0] += verts.count();
						}
					}
				} else if ( nif->isNiBlock( iShape, "bhkNiTriStripsShape" ) ) {
					bt.scale = 1;
					obj << "\r\n# bhkNiTriStripsShape\r\n\r\ng collision\r\n" << "usemtl collision\r\n\r\n";
					QModelIndex iStrips = nif->getIndex( iShape, "Strips Data" );

					for ( int r = 0; r < nif->rowCount( iStrips ); r++ )
						writeData( nif, nif->getBlockIndex( nif->getLink( nif->getIndex( iStrips, r, 0 ) ), "NiTriStripsData" ), obj, ofs, t * bt );
				}
			}
		}
	}
}

void exportObj( const NifModel * nif, const Scene* scene, const QModelIndex & index )
{
	(void) scene;
	//objCulling = Options::get()->exportCullEnabled();
	//objCullRegExp = Options::get()->cullExpression();

	//--Determine how the file will export, and be sure the user wants to continue--//
	QList<int> roots;
	QModelIndex iBlock = nif->getBlockIndex( index );

	QString question;

	if ( iBlock.isValid() ) {
		roots.append( nif->getBlockNumber( index ) );

		if ( nif->blockInherits( index, "NiNode" ) ) {
			question = nif->itemName( index ) + tr( " selected.  All children of selected node will be exported." );
		} else if ( nif->itemName( index ) == "NiTriShape" || nif->itemName( index ) == "NiTriStrips" || nif->itemName( index ) == "BSTriShape" || nif->itemName( index ) == "BSSubIndexTriShape" ) {
			question = nif->itemName( index ) + tr( " selected.  Selected mesh will be exported." );
		}
	}

	if ( question.size() == 0 ) {
		if ( nif->getBSVersion() < 100 ) {
			question = tr( "No NiNode, NiTriShape, or NiTriStrips is selected.  Entire scene will be exported." );
		} else if ( nif->getBSVersion() == 130 || nif->getBSVersion() >= 151 ) {
			question = tr( "No NiNode, BSFadeNode, or BSSubIndexTriShape is selected.  Entire scene will be exported." );
		} else {
			question = tr( "No NiNode, BSFadeNode, or BSTriShape is selected.  Entire scene will be exported." );
		}

		roots = nif->getRootLinks();
	}

	int result = QMessageBox::question( 0, tr( "Export OBJ" ), question, QMessageBox::Ok, QMessageBox::Cancel );

	if ( result == QMessageBox::Cancel ) {
		return;
	}

	//--Allow the user to select the file--//

	QSettings settings;
	settings.beginGroup( "Import-Export" );
	settings.beginGroup( "OBJ" );

	QString fname = QFileDialog::getSaveFileName( qApp->activeWindow(), tr( "Choose a .OBJ file for export" ), settings.value( "File Name" ).toString(), "OBJ (*.obj)" );

	if ( fname.isEmpty() )
		return;

	while ( fname.endsWith( ".obj", Qt::CaseInsensitive ) )
		fname = fname.left( fname.length() - 4 );

	QFile fobj( fname + ".obj" );

	if ( !fobj.open( QIODevice::WriteOnly ) ) {
		qCCritical( nsIo ) << tr( "Failed to write %1" ).arg( fobj.fileName() );
		return;
	}

	QFile fmtl( fname + ".mtl" );

	if ( !fmtl.open( QIODevice::WriteOnly ) ) {
		qCCritical( nsIo ) << tr( "Failed to write %1" ).arg( fmtl.fileName() );
		return;
	}

	fname = fmtl.fileName();
	int i = fname.lastIndexOf( "/" );

	if ( i >= 0 )
		fname = fname.remove( 0, i + 1 );

	QTextStream sobj( &fobj );
	QTextStream smtl( &fmtl );

	sobj << "# exported with NifSkope\r\n\r\n" << "mtllib " << fname << "\r\n";

	//--Translate NIF structure into file structure --//

	int ofs[3] = {
		1, 1, 1
	};
	foreach ( int l, roots ) {
		QModelIndex iBlock = nif->getBlockIndex( l );

		if ( nif->blockInherits( iBlock, "NiNode" ) )
			writeParent( nif, iBlock, sobj, smtl, ofs, Transform() );
		else if ( nif->isNiBlock( iBlock, { "NiTriShape", "NiTriStrips", "BSTriShape", "BSSubIndexTriShape" } ) )
			writeShape( nif, iBlock, sobj, smtl, ofs, Transform() );
	}

	settings.setValue( "File Name", fobj.fileName() );

	settings.endGroup(); // OBJ
	settings.endGroup(); // Import-Export

	if ( !expSknMeshWarnings.isEmpty() ) {
		if ( expSknMeshWarnings.size() > 1 ) {
			Message::warning( nullptr,
							  tr( "%1 shapes are skinned, but the OBJ format does not support skinning. "
							  "These meshes have been exported statically in their bind pose, without skin weights." ).arg( expSknMeshWarnings.size() ),
							  expSknMeshWarnings.join( "\n" )
							);
		}
		else {
			Message::warning( nullptr,
							  tr( "The shape %1 is skinned, but the OBJ format does not support skinning. "
							  "This mesh has been exported statically in its bind pose, without skin weights." ).arg( expSknMeshWarnings[0] )
							);
		}
	}

	expSknMeshWarnings.clear();
}



/*
 *  .OBJ IMPORT
 */


struct ObjPoint
{
	int v, t, n;

	bool operator==( const ObjPoint & other ) const
	{
		return v == other.v && t == other.t && n == other.n;
	}
};

struct ObjFace
{
	ObjPoint p[3];
};

struct ObjMaterial
{
	Color3 Ka, Kd, Ks;
	float d, Ns;
	QString map_Kd;
	QString map_Kn;

	ObjMaterial() : d( 1.0 ), Ns( 31.0 )
	{
	}
	inline float smoothness() const
	{
		return float( std::log2( std::min( std::max( Ns, 1.0f ), 1024.0f ) ) ) * 0.1f;
	}
};

static void readMtlLib( const QString & fname, QMap<QString, ObjMaterial> & omaterials )
{
	QFile file( fname );

	if ( !file.open( QIODevice::ReadOnly ) ) {
		qCCritical( nsIo ) << tr( "Failed to read %1" ).arg( fname );
		return;
	}

	QTextStream smtl( &file );

	QString mtlid;
	ObjMaterial mtl;

	while ( !smtl.atEnd() ) {
		QString line = smtl.readLine();

		QStringList t = line.split( " ", Qt::SkipEmptyParts );

		if ( t.value( 0 ) == "newmtl" ) {
			if ( !mtlid.isEmpty() )
				omaterials.insert( mtlid, mtl );

			mtlid = t.value( 1 );
			mtl = ObjMaterial();
		} else if ( t.value( 0 ) == "Ka" ) {
			mtl.Ka = Color3( t.value( 1 ).toDouble(), t.value( 2 ).toDouble(), t.value( 3 ).toDouble() );
		} else if ( t.value( 0 ) == "Kd" ) {
			mtl.Kd = Color3( t.value( 1 ).toDouble(), t.value( 2 ).toDouble(), t.value( 3 ).toDouble() );
		} else if ( t.value( 0 ) == "Ks" ) {
			mtl.Ks = Color3( t.value( 1 ).toDouble(), t.value( 2 ).toDouble(), t.value( 3 ).toDouble() );
		} else if ( t.value( 0 ) == "d" ) {
			mtl.d = t.value( 1 ).toDouble();
		} else if ( t.value( 0 ) == "Ns" ) {
			mtl.Ns = t.value( 1 ).toDouble();
		} else if ( t.value( 0 ) == "map_Kd" ) {
			// handle spaces in filenames
			mtl.map_Kd = t.value( 1 );

			for ( int i = 2; i < t.size(); i++ )
				mtl.map_Kd += " " + t.value( i );
		} else if ( t.value( 0 ) == "map_Kn" ) {
			// handle spaces in filenames
			mtl.map_Kn = t.value( 1 );

			for ( int i = 2; i < t.size(); i++ )
				mtl.map_Kn += " " + t.value( i );
		}
	}

	if ( !mtlid.isEmpty() )
		omaterials.insert( mtlid, mtl );
}

static void addLink( NifModel * nif, const QModelIndex & iBlock, const QString & name, qint32 link )
{
	QModelIndex iArray = nif->getIndex( iBlock, name );
	QModelIndex iSize  = nif->getIndex( iBlock, QString( "Num %1" ).arg( name ) );
	int numIndices = nif->get<int>( iSize );
	nif->set<int>( iSize, numIndices + 1 );
	nif->updateArraySize( iArray );
	nif->setLink( nif->getIndex( iArray, numIndices, 0 ), link );
}

void importObjMain( NifModel * nif, const QModelIndex & index, bool collision )
{
	//--Determine how the file will import, and be sure the user wants to continue--//

	// If no existing node is selected, create a group node.  Otherwise use selected node
	QPersistentModelIndex iNode, iShape, iStripsShape, iMaterial, iData, iTexProp, iTexSource;
	QModelIndex iBlock = nif->getBlockIndex( index );
	bool cBSShaderPPLightingProperty = false;

	//Be sure the user hasn't clicked on a NiTriStrips object
	if ( iBlock.isValid() && nif->itemName( iBlock ) == "NiTriStrips" ) {
		QMessageBox::information( 0, tr( "Import OBJ" ), tr( "You cannot import an OBJ file over a NiTriStrips object.  Please convert it to a NiTriShape object first by right-clicking and choosing Mesh > Triangulate" ) );
		return;
	}

	if ( iBlock.isValid() && nif->blockInherits( iBlock, "NiNode" ) ) {
		iNode = iBlock;
	} else if ( iBlock.isValid()
				&& (nif->itemName( iBlock ) == "NiTriShape" || nif->blockInherits( iBlock, "BSTriShape" )) ) {
		iShape = iBlock;
		//Find parent of NiTriShape
		int par_num = nif->getParent( nif->getBlockNumber( iBlock ) );

		if ( par_num != -1 ) {
			iNode = nif->getBlockIndex( par_num );
		}

		//Find material, texture, and data objects
		QList<int> children = nif->getChildLinks( nif->getBlockNumber( iShape ) );

		for ( const auto child : children ) {
			if ( child != -1 ) {
				QModelIndex temp = nif->getBlockIndex( child );
				QString type = nif->itemName( temp );

				if ( type == "BSShaderPPLightingProperty" ) {
					cBSShaderPPLightingProperty = true;
				}

				if ( type == "NiMaterialProperty" ) {
					iMaterial = temp;
				} else if ( type == "NiTriShapeData" || nif->blockInherits( temp, "BSTriShape" ) ) {
					iData = temp;
				} else if ( (type == "NiTexturingProperty") || (type == "NiTextureProperty") ) {
					iTexProp = temp;

					//Search children of texture property for texture sources/images
					QList<int> chn = nif->getChildLinks( nif->getBlockNumber( iTexProp ) );

					for ( const auto c : chn ) {
						QModelIndex temp = nif->getBlockIndex( c );
						QString type = nif->itemName( temp );

						if ( (type == "NiSourceTexture") || (type == "NiImage") ) {
							iTexSource = temp;
						}
					}
				}
			}
		}
	}

	QString question;

	if ( !collision ) {
		if ( iNode.isValid() ) {
			if ( iShape.isValid() ) {
				question = tr( "Shape selected.  The first imported mesh will replace the selected one." );
			} else {
				question = tr( "NiNode selected.  Meshes will be attached to the selected node." );
			}
		} else {
			question = tr( "No NiNode or shape selected.  Meshes will be imported to the root of the file." );
		}
	} else {
		if ( iNode.isValid() || iShape.isValid() ) {
			question = tr( "The Havok collision will be added to this object." );
		}
		question = tr( "The Havok collision will be added to the root of the file." );
	}

	int result = QMessageBox::question( 0, tr( "Import OBJ" ), question, QMessageBox::Ok, QMessageBox::Cancel );

	if ( result == QMessageBox::Cancel ) {
		return;
	}

	//--Read the file--//

	QSettings settings;
	settings.beginGroup( "Import-Export" );
	settings.beginGroup( "OBJ" );

	QString fname = QFileDialog::getOpenFileName( qApp->activeWindow(), tr( "Choose a .OBJ file to import" ), settings.value( "File Name" ).toString(), "OBJ (*.obj)" );

	if ( fname.isEmpty() )
		return;

	QFile fobj( fname );

	if ( !fobj.open( QIODevice::ReadOnly ) ) {
		qCCritical( nsIo ) << tr( "Failed to read %1" ).arg( fobj.fileName() );
		return;
	}

	QTextStream sobj( &fobj );

	QVector<Vector3> overts;
	QVector<Vector3> onorms;
	QVector<Vector2> otexco;
	QVector<ByteColor4> ocolors;
	QMap<QString, QVector<ObjFace> *> ofaces;
	QMap<QString, ObjMaterial> omaterials;

	QVector<ObjFace> * mfaces = new QVector<ObjFace>();

	QString usemtl = "None";
	ofaces.insert( usemtl, mfaces );

	while ( !sobj.atEnd() ) {
		// parse each line of the file
		QString line = sobj.readLine();

		QStringList t = line.split( " ", Qt::SkipEmptyParts );

		if ( t.value( 0 ) == "mtllib" ) {
			readMtlLib( fname.left( qMax( fname.lastIndexOf( "/" ), fname.lastIndexOf( "\\" ) ) + 1 ) + t.value( 1 ), omaterials );
		} else if ( t.value( 0 ) == "usemtl" ) {
			usemtl = t.value( 1 );
			//if ( usemtl.contains( "_" ) )
			//	usemtl = usemtl.left( usemtl.indexOf( "_" ) );

			mfaces = ofaces.value( usemtl );

			if ( !mfaces ) {
				mfaces = new QVector<ObjFace>();
				ofaces.insert( usemtl, mfaces );
			}
		} else if ( t.value( 0 ) == "v" ) {
			overts.append( Vector3( t.value( 1 ).toDouble(), t.value( 2 ).toDouble(), t.value( 3 ).toDouble() ) );
			ByteColor4 c;
			if ( t.size() == 7 ) {
				// X, Y, Z, R, G, B format
				c[0] = t.value( 4 ).toFloat();
				c[1] = t.value( 5 ).toFloat();
				c[2] = t.value( 6 ).toFloat();
			}
			ocolors.append( c );
		} else if ( t.value( 0 ) == "vt" ) {
			otexco.append( Vector2( t.value( 1 ).toDouble(), 1.0 - t.value( 2 ).toDouble() ) );
		} else if ( t.value( 0 ) == "vn" ) {
			onorms.append( Vector3( t.value( 1 ).toDouble(), t.value( 2 ).toDouble(), t.value( 3 ).toDouble() ) );
		} else if ( t.value( 0 ) == "f" ) {
			if ( t.count() > 5 ) {
				qCCritical( nsNif ) << tr( "Please triangulate your mesh before import." );
				return;
			}

			for ( int j = 1; j < t.count() - 2; j++ ) {
				ObjFace face;

				for ( int i = 0; i < 3; i++ ) {
					QStringList lst = t.value( i == 0 ? 1 : j + i ).split( "/" );

					int v = lst.value( 0 ).toInt();
					if ( v < 0 )
						v += overts.count();
					else
						v--;

					int t = lst.value( 1 ).toInt();
					if ( t < 0 )
						v += otexco.count();
					else
						t--;

					int n = lst.value( 2 ).toInt();
					if ( n < 0 )
						n += onorms.count();
					else
						n--;

					face.p[i].v = v;
					face.p[i].t = t;
					face.p[i].n = n;
				}

				mfaces->append( face );
			}
		}
	}

	//--Translate file structures into NIF ones--//

	if ( iNode.isValid() == false ) {
		iNode = nif->insertNiBlock( "NiNode" );
		nif->set<QString>( iNode, "Name", "Scene Root" );
	}

	// create a NiTriShape or BSTriShape for each material in the object
	int shapecount = 0;
	bool first_tri_shape = true;
	QMapIterator<QString, QVector<ObjFace> *> it( ofaces );

	nif->holdUpdates( true );

	while ( it.hasNext() ) {
		it.next();

		if ( !it.value()->count() )
			continue;

		QVector<Vector3> verts;
		QVector<Vector3> norms;
		QVector<Vector2> texco;
		QVector<ByteColor4> colors;
		QVector<Triangle> triangles;

		QVector<ObjPoint> points;
		bool	haveVertexColors = false;

		foreach ( ObjFace oface, *( it.value() ) ) {
			Triangle tri;

			for ( int t = 0; t < 3; t++ ) {
				ObjPoint p = oface.p[t];
				int ix;

				for ( ix = 0; ix < points.count(); ix++ ) {
					if ( points[ix] == p )
						break;
				}

				if ( ix == points.count() ) {
					points.append( p );
					verts.append( overts.value( p.v ) );
					norms.append( onorms.value( p.n ) );
					texco.append( otexco.value( p.t ) );
					ByteColor4	c = ocolors.value( p.v );
					colors.append( c );
					haveVertexColors = haveVertexColors || ( std::uint32_t( c ) != 0xFFFFFFFFU );
				}

				tri[t] = ix;
			}

			triangles.append( tri );
		}

		if ( !collision ) {
			//If we are on the first shape, and one was selected in the 3D view, use the existing one
			bool newiShape = false;

			if ( iShape.isValid() == false || first_tri_shape == false ) {
				iShape = nif->insertNiBlock( nif->getBSVersion() < 100 ? "NiTriShape" : "BSTriShape" );
				newiShape = true;
			}

			if ( newiShape ) {
				// don't change a name what already exists; // don't add duplicates
				nif->set<QString>( iShape, "Name", QString( "%1:%2" ).arg( nif->get<QString>( iNode, "Name" ) ).arg( shapecount++ ) );
				addLink( nif, iNode, "Children", nif->getBlockNumber( iShape ) );
				if ( nif->getBSVersion() >= 100 ) {
					iData = iShape;
					nif->set<quint32>( iShape, "Flags", 14 );
				}
			}

			if ( !omaterials.contains( it.key() ) ) {
				Message::append( tr( "Warnings were generated during OBJ import." ),
					tr( "Material '%1' not found in mtllib." ).arg( it.key() ) );
			}

			ObjMaterial mtl = omaterials.value( it.key() );

			QModelIndex shaderProp;
			// add material property, for non-Skyrim versions
			if ( nif->getBSVersion() <= 34 ) {
				bool newiMaterial = false;

				if ( iMaterial.isValid() == false || first_tri_shape == false ) {
					iMaterial = nif->insertNiBlock( "NiMaterialProperty" );
					newiMaterial = true;
				}

				if ( newiMaterial ) // don't affect a property  that is already there - that name is generated above on export and it has nothign to do with the stored name
					nif->set<QString>( iMaterial, "Name", it.key() );

				nif->set<Color3>( iMaterial, "Ambient Color", mtl.Ka );
				nif->set<Color3>( iMaterial, "Diffuse Color", mtl.Kd );
				nif->set<Color3>( iMaterial, "Specular Color", mtl.Ks );

				if ( newiMaterial ) // don't affect a property  that is already there
					nif->set<Color3>( iMaterial, "Emissive Color", Color3( 0, 0, 0 ) );

				nif->set<float>( iMaterial, "Alpha", mtl.d );
				nif->set<float>( iMaterial, "Glossiness", mtl.Ns );

				if ( newiMaterial ) // don't add property that is already there
					addLink( nif, iShape, "Properties", nif->getBlockNumber( iMaterial ) );
			} else {
				if ( !newiShape )
					shaderProp = nif->getBlockIndex( nif->getLink( iShape, "Shader Property" ) );
				if ( shaderProp.isValid() ) {
					if ( nif->getBSVersion() >= 151 )
						shaderProp = nif->getIndex( shaderProp, "Shader Property Data" );
				} else {
					shaderProp = nif->insertNiBlock( "BSLightingShaderProperty" );
					nif->setLink( iShape, "Shader Property", nif->getBlockNumber( shaderProp ) );
					if ( nif->getBSVersion() >= 151 ) {
						shaderProp = nif->getIndex( shaderProp, "Shader Property Data" );
						nif->set<quint32>( shaderProp, "Num SF1", quint32( haveVertexColors ) + 4 );
						QModelIndex	iSF1 = nif->getIndex( shaderProp, "SF1" );
						nif->updateArraySize( iSF1 );
						nif->set<quint32>( nif->getIndex( iSF1, 0 ), ShaderFlags::CAST_SHADOWS );
						nif->set<quint32>( nif->getIndex( iSF1, 1 ), ShaderFlags::ZBUFFER_TEST );
						nif->set<quint32>( nif->getIndex( iSF1, 2 ), ShaderFlags::ZBUFFER_WRITE );
						nif->set<quint32>( nif->getIndex( iSF1, 3 ), ShaderFlags::PBR );
						if ( haveVertexColors )
							nif->set<quint32>( nif->getIndex( iSF1, 4 ), ShaderFlags::VERTEXCOLORS );
					}
				}

				if ( nif->getBSVersion() < 130 )
					nif->set<float>( shaderProp, "Glossiness", mtl.Ns );
				else
					nif->set<float>( shaderProp, "Smoothness", mtl.smoothness() );
				nif->set<Color3>( shaderProp, "Specular Color", mtl.Ks );
			}

			if ( !mtl.map_Kd.isEmpty() ) {
				if ( nif->getBSVersion() > 34 && shaderProp.isValid() ) {
					auto textureSet = nif->insertNiBlock( "BSShaderTextureSet" );
					nif->setLink( shaderProp, "Texture Set", nif->getBlockNumber( textureSet ) );

					if ( nif->getBSVersion() == 130 )
						nif->set<uint>( textureSet, "Num Textures", 10 );
					else
						nif->set<uint>( textureSet, "Num Textures", 9 );

					auto iTextures = nif->getIndex( textureSet, "Textures" );
					nif->updateArraySize( iTextures );

					nif->set<QString>( nif->getIndex( iTextures, 0 ), mtl.map_Kd );
					nif->set<QString>( nif->getIndex( iTextures, 1 ), mtl.map_Kn );
				} else if ( nif->getVersionNumber() >= 0x0303000D ) {
					//Newer versions use NiTexturingProperty and NiSourceTexture
					if ( iTexProp.isValid() == false || first_tri_shape == false || nif->itemStrType( iTexProp ) != "NiTexturingProperty" ) {
						if ( !cBSShaderPPLightingProperty ) // no need of NiTexturingProperty when BSShaderPPLightingProperty is present
							iTexProp = nif->insertNiBlock( "NiTexturingProperty" );
					}

					QModelIndex iBaseMap;

					if ( !cBSShaderPPLightingProperty ) {
						// no need of NiTexturingProperty when BSShaderPPLightingProperty is present
						addLink( nif, iShape, "Properties", nif->getBlockNumber( iTexProp ) );

						nif->set<int>( iTexProp, "Has Base Texture", 1 );
						iBaseMap = nif->getIndex( iTexProp, "Base Texture" );
						nif->set<int>( iBaseMap, "Clamp Mode", 3 );
						nif->set<int>( iBaseMap, "Filter Mode", 2 );
					}

					if ( iTexSource.isValid() == false || first_tri_shape == false || nif->itemStrType( iTexSource ) != "NiSourceTexture" ) {
						if ( !cBSShaderPPLightingProperty )
							iTexSource = nif->insertNiBlock( "NiSourceTexture" );
					}

					if ( !cBSShaderPPLightingProperty ) {
						// no need of NiTexturingProperty when BSShaderPPLightingProperty is present
						nif->setLink( iBaseMap, "Source", nif->getBlockNumber( iTexSource ) );
						QModelIndex	iFmtPrefs = nif->getIndex( iTexSource, "Format Prefs" );
						if ( iFmtPrefs.isValid() ) {
							nif->set<int>( iFmtPrefs, "Pixel Layout", nif->getVersion() == "20.0.0.5" ? 6 : 5 );
							nif->set<int>( iFmtPrefs, "Use Mipmaps", 2 );
							nif->set<int>( iFmtPrefs, "Alpha Format", 3 );
						}

						nif->set<int>( iTexSource, "Use External", 1 );
						nif->set<QString>( iTexSource, "File Name", TexCache::stripPath( mtl.map_Kd, nif->getFolder() ) );
					}
				} else {
					//Older versions use NiTextureProperty and NiImage
					if ( iTexProp.isValid() == false || first_tri_shape == false || nif->itemStrType( iTexProp ) != "NiTextureProperty" ) {
						iTexProp = nif->insertNiBlock( "NiTextureProperty" );
					}

					addLink( nif, iShape, "Properties", nif->getBlockNumber( iTexProp ) );

					if ( iTexSource.isValid() == false || first_tri_shape == false || nif->itemStrType( iTexSource ) != "NiImage" ) {
						iTexSource = nif->insertNiBlock( "NiImage" );
					}

					nif->setLink( iTexProp, "Image", nif->getBlockNumber( iTexSource ) );

					nif->set<int>( iTexSource, "External", 1 );
					nif->set<QString>( iTexSource, "File Name", TexCache::stripPath( mtl.map_Kd, nif->getFolder() ) );
				}
			}

			if ( nif->getBSVersion() < 100 ) {
				// NiTriShape
				if ( iData.isValid() == false || first_tri_shape == false ) {
					iData = nif->insertNiBlock( "NiTriShapeData" );
				}

				nif->setLink( iShape, "Data", nif->getBlockNumber( iData ) );

				nif->set<int>( iData, "Has Vertices", 1 );
				nif->set<int>( iData, "Num Vertices", verts.count() );
				nif->updateArraySize( iData, "Vertices" );
				nif->setArray<Vector3>( iData, "Vertices", verts );
				nif->set<int>( iData, "Has Normals", 1 );
				nif->updateArraySize( iData, "Normals" );
				nif->setArray<Vector3>( iData, "Normals", norms );
				// the following fields are version dependent
				if ( auto i = nif->getItem( iData, "Has UV" ); i )
					nif->set<int>( i, 1 );
				if ( auto i = nif->getItem( iData, "Num UV Sets" ); i )
					nif->set<int>( i, 1 );
				if ( auto i = nif->getItem( iData, "Data Flags" ); i )
					nif->set<int>( i, 4097 );
				if ( auto i = nif->getItem( iData, "BS Data Flags" ); i )
					nif->set<int>( i, 4097 );

				if ( nif->getBSVersion() > 34 ) {
					nif->set<int>( iData, "Has Vertex Colors", 1 );
					nif->updateArraySize( iData, "Vertex Colors" );
				}

				QModelIndex iTexCo = nif->getIndex( iData, "UV Sets" );
				nif->updateArraySize( iTexCo );
				nif->updateArraySize( nif->getIndex( iTexCo, 0, 0 ) );
				nif->setArray<Vector2>( nif->getIndex( iTexCo, 0, 0 ), texco );

				nif->set<int>( iData, "Has Triangles", 1 );
				nif->set<int>( iData, "Num Triangles", triangles.count() );
				nif->set<int>( iData, "Num Triangle Points", triangles.count() * 3 );
				nif->updateArraySize( iData, "Triangles" );
				nif->setArray<Triangle>( iData, "Triangles", triangles );

				nif->set<int>( iData, "Consistency Flags", 0x4000 );	// CT_STATIC
			} else {
				// BSTriShape
				// 28 or 32 bytes: position as floats (16), UV (4), normal (4), tangent (4), color (4)
				// TODO: use half precision?
				std::uint64_t	vertexDesc =  0x0001B00000650407ULL;
				if ( nif->getBSVersion() >= 130 )
					vertexDesc = vertexDesc | 0x0040000000000000ULL;	// full precision flag for Fallout 4 and 76
				if ( haveVertexColors )
					vertexDesc = vertexDesc + 0x0002000007000001ULL;	// enable vertex colors
				nif->set<BSVertexDesc>( iShape, "Vertex Desc", vertexDesc );
				nif->set<quint32>( iShape, "Num Triangles", quint32( triangles.size() ) );
				nif->set<quint32>( iShape, "Num Vertices", quint32( verts.size() ) );
				nif->set<quint32>( iShape, "Data Size",
									size_t( verts.size() ) * ( !haveVertexColors ? 28 : 32 )
									+ size_t( triangles.size() ) * 6 );

				nif->setState( BaseModel::Processing );

				QModelIndex	iVerts = nif->getIndex( iShape, "Vertex Data" );
				nif->updateArraySize( iVerts );
				for ( qsizetype i = 0; i < verts.size(); i++ ) {
					QModelIndex	iVertex = nif->getIndex( iVerts, int( i ) );
					if ( !iVertex.isValid() )
						continue;
					nif->set<Vector3>( iVertex, "Vertex", verts.at( i ) );
					nif->set<HalfVector2>( iVertex, "UV", texco.at( i ) );
					nif->set<ByteVector3>( iVertex, "Normal", ByteVector3( norms.at( i ) ) );
					QModelIndex	j = nif->getIndex( iVertex, "Vertex Colors" );
					if ( j.isValid() )
						nif->set<ByteColor4>( j, colors.at( i ) );
				}

				QModelIndex	iTriangles = nif->getIndex( iShape, "Triangles" );
				nif->updateArraySize( iTriangles );
				nif->setArray<Triangle>( iTriangles, triangles );

				nif->restoreState();
			}

			// "find me a center": see nif.xml for details
			BoundSphere	bounds( verts, true );
			bounds.update( nif, iData );
		} else if ( nif->getBSVersion() > 0 ) {
			// create experimental havok collision mesh
			QVector<Vector3> verts;
			QVector<Vector3> norms;
			QVector<Triangle> triangles;

			QVector<ObjPoint> points;

			shapecount++;

			for ( const ObjFace & oface : *( it.value() ) ) {
				Triangle tri;

				for ( int t = 0; t < 3; t++ ) {
					ObjPoint p = oface.p[t];
					int ix;

					for ( ix = 0; ix < points.count(); ix++ ) {
						if ( points[ix] == p )
							break;
					}

					if ( ix == points.count() ) {
						points.append( p );
						verts.append( overts.value( p.v ) );
						norms.append( onorms.value( p.n ) );
					}

					tri[t] = ix;
				}

				triangles.append( tri );
			}

			QPersistentModelIndex iData = nif->insertNiBlock( "NiTriStripsData" );

			nif->set<int>( iData, "Has Vertices", 1 );
			nif->set<int>( iData, "Num Vertices", verts.count() );
			nif->updateArraySize( iData, "Vertices" );
			nif->setArray<Vector3>( iData, "Vertices", verts );
			nif->set<int>( iData, "Has Normals", 1 );
			nif->updateArraySize( iData, "Normals" );
			nif->setArray<Vector3>( iData, "Normals", norms );

			BoundSphere	bounds( verts, true );
			bounds.update( nif, iData );

			// do not stitch, because it looks better in the cs
			QVector<QVector<quint16> > strips = stripify( triangles, false );

			nif->set<int>( iData, "Num Strips", strips.count() );
			nif->set<int>( iData, "Has Points", 1 );

			QModelIndex iLengths = nif->getIndex( iData, "Strip Lengths" );
			QModelIndex iPoints  = nif->getIndex( iData, "Points" );

			if ( iLengths.isValid() && iPoints.isValid() ) {
				nif->updateArraySize( iLengths );
				nif->updateArraySize( iPoints );
				int x = 0;
				int z = 0;
				for ( const QVector<quint16> & strip : strips ) {
					nif->set<int>( nif->getIndex( iLengths, x, 0 ), strip.count() );
					QModelIndex iStrip = nif->getIndex( iPoints, x, 0 );
					nif->updateArraySize( iStrip );
					nif->setArray<quint16>( iStrip, strip );
					x++;
					z += strip.count() - 2;
				}
				nif->set<int>( iData, "Num Triangles", z );
			}

			if ( shapecount == 1 ) {
				iStripsShape = nif->insertNiBlock( "bhkNiTriStripsShape" );

				// For some reason need to update all the fixed arrays...
				nif->updateArraySize( iStripsShape, "Unused 01" );
				nif->set<int>( iStripsShape, "Num Strips Data", 0 );
				nif->updateArraySize( iStripsShape, "Strips Data" );

				QPersistentModelIndex iBody = nif->insertNiBlock( "bhkRigidBody" );
				nif->setLink( iBody, "Shape", nif->getBlockNumber( iStripsShape ) );
				for( int i = 0; i < nif->rowCount( iBody ); i++ ) {
					auto iChild = nif->getIndex( iBody, i, 0 );
					if ( nif->isArray( iChild ) )
						nif->updateArraySize( iChild );
				}

				QPersistentModelIndex iObject = nif->insertNiBlock( "bhkCollisionObject" );

				QPersistentModelIndex iParent = (iShape.isValid()) ? iShape : iNode;
				nif->setLink( iObject, "Target", nif->getBlockNumber( iParent ) );
				nif->setLink( iObject, "Body", nif->getBlockNumber( iBody ) );

				nif->setLink( iParent, "Collision Object", nif->getBlockNumber( iObject ) );
			}

			if ( shapecount >= 1 ) {
				addLink( nif, iStripsShape, "Strips Data", nif->getBlockNumber( iData ) );
				nif->set<int>( iStripsShape, "Num Filters", shapecount );
				nif->updateArraySize( iStripsShape, "Filters" );
			}
		}

		spTangentSpace TSpacer;
		TSpacer.castIfApplicable( nif, iShape );

		//Finished with the first shape which is the only one that can import over the top of existing data
		first_tri_shape = false;
	}
	nif->holdUpdates( false );

	qDeleteAll( ofaces );

	settings.setValue( "File Name", fname );

	settings.endGroup(); // OBJ
	settings.endGroup(); // Import-Export

	nif->reset();

	// center view
	NifSkope *	w = dynamic_cast< NifSkope * >( nif->getWindow() );
	if ( w )
		w->on_aViewCenter_triggered();
}

void importObj( NifModel* nif, const QModelIndex& index )
{
	importObjMain(nif, index, false);
}

void importObjAsCollision( NifModel* nif, const QModelIndex& index )
{
	importObjMain(nif, index, true);
}
