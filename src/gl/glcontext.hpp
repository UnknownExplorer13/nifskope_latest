/***** BEGIN LICENSE BLOCK *****

BSD License

Copyright (c) 2005-2024, NIF File Format Library and Tools
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

#ifndef GLCONTEXT_HPP_INCLUDED
#define GLCONTEXT_HPP_INCLUDED

#include <QString>
#include <QVector>
#include <QMap>
#include <QModelIndex>
#include <QOpenGLFunctions>

#include "data/niftypes.h"

class NifModel;
class QOpenGLContext;

class NifSkopeOpenGLContext
{
protected:
	//! Base Condition class for shader programs
	class Condition
	{
public:
		Condition() {}
		virtual ~Condition() {}

		virtual bool eval( const NifModel * nif, const QVector<QModelIndex> & iBlocks ) const = 0;
	};

	//! Condition class for single conditions
	class ConditionSingle final : public Condition
	{
public:
		ConditionSingle( const QString & line, bool neg = false );

		bool eval( const NifModel * nif, const QVector<QModelIndex> & iBlocks ) const override final;

protected:
		QString left, right;
		enum Type
		{
			NONE, EQ, NE, LE, GE, LT, GT, AND, NAND
		};
		Type comp;
		const static QHash<Type, QString> compStrs;

		bool invert;

		QModelIndex getIndex( const NifModel * nif, const QVector<QModelIndex> & iBlock, QString name ) const;
		template <typename T> bool compare( T a, T b ) const;
	};

	//! Condition class for grouped conditions (OR or AND)
	class ConditionGroup final : public Condition
	{
public:
		ConditionGroup( bool o = false ) { _or = o; }
		~ConditionGroup() { qDeleteAll( conditions ); }

		bool eval( const NifModel * nif, const QVector<QModelIndex> & iBlocks ) const override final;

		void addCondition( Condition * c );

		bool isOrGroup() const { return _or; }

protected:
		QVector<Condition *> conditions;
		bool _or;
	};

	//! Parsing and loading of .frag or .vert files
	class Shader
	{
public:
		Shader( const QString & name, unsigned int type, QOpenGLFunctions * fn );
		~Shader();

		bool load( const QString & filepath );

		QOpenGLFunctions * f;
		QString name;
		unsigned int id;
		bool status;

protected:
		unsigned int type;
	};

public:
	//! Parsing and loading of .prog files
	class Program
	{
public:
		Program( const QString & name, QOpenGLFunctions * fn );
		~Program();

		bool load( const QString & filepath, NifSkopeOpenGLContext * context );

		QOpenGLFunctions * f;
		QString name;
		unsigned int id;
		bool status = false;

		ConditionGroup conditions;

private:
		struct UniformLocationMapItem {
			const char *	fmt;
			std::uint32_t	args;
			int	l;
			inline UniformLocationMapItem()
				: fmt( nullptr ), args( 0 ), l( -1 )
			{
			}
			inline UniformLocationMapItem( const char *s, int argsX16Y16 );
			inline bool operator==( const UniformLocationMapItem & r ) const;
			inline std::uint32_t hashFunction() const;
		};
		UniformLocationMapItem *	uniLocationsMap;
		unsigned int	uniLocationsMapMask;
		unsigned int	uniLocationsMapSize;
		int storeUniformLocation( const UniformLocationMapItem & o, size_t i );
public:
		// fmt must be a string literal, with at most two %d format integer arguments in the range 0 to 99
		int uniLocation( const char * fmt );
		int uniLocation( const char * fmt, int argsX16Y16 );
		inline int uniLocation( const char * fmt, int arg1, int arg2 )
		{
			return uniLocation( fmt, arg1 | ( arg2 << 16 ) );
		}
		// name must be a string literal
		void uni1i( const char * name, int x );
		inline void uni1b( const char * name, bool x )
		{
			uni1i( name, int(x) );
		}
		void uni1f( const char * name, float x );
		// l = location returned by uniLocation()
		void uni1b_l( int l, bool x );
		void uni1i_l( int l, int x );
		void uni1f_l( int l, float x );
		void uni2f_l( int l, float x, float y );
		void uni3f_l( int l, float x, float y, float z );
		void uni4f_l( int l, FloatVector4 x );
		void uni4srgb_l( int l, FloatVector4 x );
		void uni4c_l( int l, std::uint32_t c, bool isSRGB = false );
		void uni1bv_l( int l, const bool * x, size_t n );
		void uni1iv_l( int l, const int * x, size_t n );
		void uni1fv_l( int l, const float * x, size_t n );
		void uni4fv_l( int l, const FloatVector4 * x, size_t n );
		void uni3m_l( int l, const Matrix & val );
		void uni4m_l( int l, const Matrix4 & val );
		void uniSampler_l( int l, int firstTextureUnit, int textureCnt, int arraySize );

		inline void uni2f( const char * name, float x, float y )
		{
			uni2f_l( uniLocation( name ), x, y );
		}
		inline void uni3f( const char * name, float x, float y, float z )
		{
			uni3f_l( uniLocation( name ), x, y, z );
		}
		inline void uni4f( const char * name, FloatVector4 x )
		{
			uni4f_l( uniLocation( name ), x );
		}
		inline void uni4srgb( const char * name, FloatVector4 x )
		{
			uni4srgb_l( uniLocation( name ), x );
		}
		inline void uni4c( const char * name, std::uint32_t c, bool isSRGB = false )
		{
			uni4c_l( uniLocation( name ), c, isSRGB );
		}
		inline void uni1bv( const char * name, const bool * x, size_t n )
		{
			uni1bv_l( uniLocation( name ), x, n );
		}
		inline void uni1iv( const char * name, const int * x, size_t n )
		{
			uni1iv_l( uniLocation( name ), x, n );
		}
		inline void uni1fv( const char * name, const float * x, size_t n )
		{
			uni1fv_l( uniLocation( name ), x, n );
		}
		inline void uni4fv( const char * name, const FloatVector4 * x, size_t n )
		{
			uni4fv_l( uniLocation( name ), x, n );
		}
		inline void uni3m( const char * name, const Matrix & val )
		{
			uni3m_l( uniLocation( name ), val );
		}
		inline void uni4m( const char * name, const Matrix4 & val )
		{
			uni4m_l( uniLocation( name ), val );
		}
		bool uniSampler( class BSShaderLightingProperty * bsprop, const char * var, int textureSlot,
							int & texunit, const QString & alternate, uint clamp, const QString & forced = {} );
	};

protected:
	QMap<QString, Shader *> shaders;
	QMap<QString, Program *> programs;

public:
	NifSkopeOpenGLContext( QOpenGLContext * context );
	~NifSkopeOpenGLContext();

	//! Updates shaders
	void updateShaders();
	//! Releases shaders
	void releaseShaders();
	//! Select shader program to use
	Program * useProgram( const QString & name );
	//! Stop shader program
	void stopProgram();

	//! Context Functions
	QOpenGLFunctions	fn;
	//! Context
	QOpenGLContext *	cx;
};

#endif

