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

#ifndef UVEDIT_H
#define UVEDIT_H

#include "data/niftypes.h"

#include <QOpenGLWidget> // Inherited
#include <QDialog>   // Inherited
#include <QModelIndex>
#include <QPersistentModelIndex>
#include <QPointer>

#include <math.h>


class NifModel;
class NifSkopeOpenGLContext;
class TexCache;

class QActionGroup;
class QCheckBox;
class QDoubleSpinBox;
class QGridLayout;
class QMenu;
class QUndoStack;

namespace Game
{
	enum GameMode : int;
}

#undef None // conflicts with Qt

//! Displays and allows editing of UV coordinate data
class UVWidget final : public QOpenGLWidget
{
	Q_OBJECT

protected:
	UVWidget( QWidget * parent = nullptr );
	~UVWidget();

	// for BSLightingShaderProperty and BSEffectShaderProperty
	void setTexturePaths( NifModel * nif, QModelIndex iTexProp );

public:
	//! Creates the UV editor widget
	static UVWidget * createEditor( NifModel * nif, const QModelIndex & index );

	//! Sets the NIF data
	bool setNifData( NifModel * nif, const QModelIndex & index );

	//! From QWidget; the recommended size of the widget
	QSize sizeHint() const override final;
	//! From QWidget; the minimum size of the widget
	QSize minimumSizeHint() const override final;

	//! Sets the size hint
	void setSizeHint( const QSize & s );

	//! Returns the preferred height for this widget, given the width w.
	int heightForWidth( int width ) const override final;

	//! The actual dimensions in pixels
	int	pixelWidth = 640;
	int	pixelHeight = 480;

	//! For future use in realtime mouse-driven scaling
	enum EditingMode
	{
		None,
		Move,
		Scale
	} editmode;

protected:
	void initializeGL() override final;
	void resizeGL( int width, int height ) override final;
	void paintGL() override final;

	void mousePressEvent( QMouseEvent * e ) override final;
	void mouseReleaseEvent( QMouseEvent * e ) override final;
	void mouseMoveEvent( QMouseEvent * e ) override final;
	void wheelEvent( QWheelEvent * e ) override final;

	void keyPressEvent( QKeyEvent * ) override final;
	void keyReleaseEvent( QKeyEvent * ) override final;

public slots:
	//! Does the selection contain this vertex?
	bool isSelected( int index );
	//! Select a vertex
	void select( int index, bool yes = true );
	//! Select a region containing vertices
	void select( const QRegion & r, bool add = true );
	//! Select all
	void selectAll();
	//! Select none
	void selectNone();
	//! Select faces that use the currently selected vertices
	void selectFaces();
	//! Select vertices connected to the currently selected vertices
	void selectConnected();
	//! Move the selection by the specified vector
	void moveSelection( double dx, double dy );
	//! Scale the selection
	void scaleSelection();
	//! Rotate the selection
	void rotateSelection();
	//! Export Starfield .mesh file
	void exportSFMesh();

	void updateSettings();
	void update_Blend( [[maybe_unused]] bool tmp ) { update(); }
	void update_3D() { update(); }

protected slots:
	void nifDataChanged( const QModelIndex & );
	//! Build the texture slots menu
	void getTexSlots();
	//! Select a texture slot
	void selectTexSlot();
	//! Build the coordinate sets menu
	void getCoordSets();
	//! Select a coordinate set
	void selectCoordSet();
	//! Change the coordinate set in use
	void changeCoordSet( int setToUse );
	//! Duplicate a coordinate set
	void duplicateCoordSet();

private:
	//! List of selected vertices
	QList<int> selection;

	QRect selectRect;
	QList<QPoint> selectPoly;
	unsigned int selectCycle = 0;

	QVector<Vector2> texcoords;
	QVector<Triangle> faces;
	QMultiMap<int, int> texcoords2faces;
	QVector<Vector3> vertexPosBuf;
	QVector<FloatVector4> vertexColorBuf;

	QSize sHint;

	struct TextureInfo {
		QString	name;
		int	clampMode;	// 0 = wrap, 1 = clamp, 2 = mirror, 3 = border
		int	colorMode;	// 0 = linear, 1 = sRGB, 2 = BC5_UNORM, 3 = BC5_SNORM
		FloatVector4	scaleAndOffset;
		TextureInfo()
			: clampMode( 0 ), colorMode( 0 ), scaleAndOffset( 1.0f, 1.0f, 0.0f, 0.0f )
		{
		}
		TextureInfo( const NifModel * nif, const QString & texturePath );
		TextureInfo( const NifModel * nif, const std::string_view * texturePath, const void * uvStream );
		~TextureInfo()
		{
		}
	};
	NifSkopeOpenGLContext * cx;
	TexCache * textures;
	QVector<TextureInfo>	texfiles;
	QModelIndex texsource;

	void drawTexCoords();

	void updateViewRect( int width, int height );
	bool bindTexture( const TextureInfo & t );
	bool bindTexture( const QModelIndex & iSource );

	QVector<int> indices( const QPoint & p ) const;
	QVector<int> indices( const QRegion & r ) const;

	QPoint mapFromContents( const Vector2 & v ) const;
	Vector2 mapToContents( const QPoint & p ) const;

	void updateNif();

	NifModel * nif;
	QPersistentModelIndex iShape, iShapeData, iTexCoords, iTex, iPartBlock;

	//! If mesh is skinned, different behavior is required for stream version 100
	bool isDataOnSkin = false;

	//! Submenu for texture slot selection
	QMenu * menuTexSelect;
	//! Group that holds texture slot selection actions
	QActionGroup * texSlotGroup;
	//! List of valid texture slots in texnames
	QVector<int> validTexs;

	//! Names of texture slots
	static QStringList texnames;

	//! Texture file currently being operated on
	int currentTexFile = 0;
	//! Read texcoords from the nif
	bool setTexCoords( const QVector<Triangle> * triangles = nullptr );
	//! Coordinate set currently in use
	int currentCoordSet = 0;

	//! Submenu for coordinate set selection
	QMenu * coordSetSelect = nullptr;
	//! Group that holds coordinate set actions
	QActionGroup * coordSetGroup;
	//! Action to trigger duplication of current coordinate set
	QAction * aDuplicateCoords;

	// scale and offset to map viewport coordinates (0.0,0.0 = top left, 1.0,1.0 = bottom right) to texture coordinates
	FloatVector4 viewScaleAndOffset;
	// texture coordinate offset, the texture is centered with pos = 0.0,0.0
	double pos[2];

	QPoint mousePos;
	QHash<int, bool> kbd;

	double zoom;

	QUndoStack * undoStack;

	friend class UVWSelectCommand;
	friend class UVWMoveCommand;
	friend class UVWScaleCommand;
	friend class UVWRotateCommand;

	QAction * aTextureBlend;

	QPersistentModelIndex	sfMeshIndex;

	struct Settings
	{
		Color4 background;
		Color4 highlight;
		Color4 wireframe;
	} cfg;
};

//! Dialog for getting scaling factors
class ScalingDialog final : public QDialog
{
	Q_OBJECT

public:
	ScalingDialog( QWidget * parent = nullptr );

protected:
	/*
	QVBoxLayout * vbox;
	QHBoxLayout * hbox;
	QHBoxLayout * btnbox;
	*/
	QGridLayout * grid;
	QDoubleSpinBox * spinXScale;
	QDoubleSpinBox * spinYScale;
	QCheckBox * uniform;
	QDoubleSpinBox * spinXMove;
	QDoubleSpinBox * spinYMove;

public slots:
	float getXScale();
	float getYScale();
	float getXMove();
	float getYMove();

protected slots:
	void setUniform( bool status );
};

#endif
