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

#include "glcontext.hpp"
#include "ddstxt16.hpp"
#include "libfo76utils/src/filebuf.hpp"
#include "model/nifmodel.h"
#include "gltex.h"
#include "glproperty.h"

#include <QDir>
#include <QOpenGLContext>
#include <QOpenGLVersionFunctionsFactory>


static const QString white = "#FFFFFFFF";
static const QString black = "#FF000000";
static const QString lighting = "#FF00F040";
static const QString reflectivity = "#FF0A0A0A";
static const QString default_n = "#FFFF8080";
static const QString default_ns = "#FFFF8080n";


// Templates

template <typename T> inline bool NifSkopeOpenGLContext::ConditionSingle::compare( T a, T b ) const
{
	switch ( comp ) {
	case EQ:
		return a == b;
	case NE:
		return a != b;
	case LE:
		return a <= b;
	case GE:
		return a >= b;
	case LT:
		return a < b;
	case GT:
		return a > b;
	case AND:
		return a & b;
	case NAND:
		return !(a & b);
	default:
		return true;
	}
}

template <> inline bool NifSkopeOpenGLContext::ConditionSingle::compare( float a, float b ) const
{
	switch ( comp ) {
	case EQ:
		return a == b;
	case NE:
		return a != b;
	case LE:
		return a <= b;
	case GE:
		return a >= b;
	case LT:
		return a < b;
	case GT:
		return a > b;
	default:
		return true;
	}
}

template <> inline bool NifSkopeOpenGLContext::ConditionSingle::compare( QString a, QString b ) const
{
	switch ( comp ) {
	case EQ:
		return a == b;
	case NE:
		return a != b;
	default:
		return false;
	}
}


const QHash<NifSkopeOpenGLContext::ConditionSingle::Type, QString> NifSkopeOpenGLContext::ConditionSingle::compStrs{
	{ EQ,  " == " },
	{ NE,  " != " },
	{ LE,  " <= " },
	{ GE,  " >= " },
	{ LT,  " < " },
	{ GT,  " > " },
	{ AND, " & " },
	{ NAND, " !& " }
};

NifSkopeOpenGLContext::ConditionSingle::ConditionSingle( const QString & line, bool neg ) : invert( neg )
{
	QHashIterator<Type, QString> i( compStrs );
	int pos = -1;

	while ( i.hasNext() ) {
		i.next();
		pos = line.indexOf( i.value() );

		if ( pos > 0 )
			break;
	}

	if ( pos > 0 ) {
		left  = line.left( pos ).trimmed();
		right = line.right( line.length() - pos - i.value().length() ).trimmed();

		if ( right.startsWith( "\"" ) && right.endsWith( "\"" ) )
			right = right.mid( 1, right.length() - 2 );

		comp = i.key();
	} else {
		left = line;
		comp = NONE;
	}
}

QModelIndex NifSkopeOpenGLContext::ConditionSingle::getIndex( const NifModel * nif, const QVector<QModelIndex> & iBlocks, QString blkid ) const
{
	QString childid;

	if ( blkid.startsWith( QLatin1StringView("HEADER/") ) ) {
		auto blk = blkid.remove( "HEADER/" );
		if ( blk.contains("/") ) {
			auto blks = blk.split( "/" );
			return nif->getIndex( nif->getIndex( nif->getHeaderIndex(), blks.at(0) ), blks.at(1) );
		}
		return nif->getIndex( nif->getHeaderIndex(), blk );
	}

	int pos = blkid.indexOf( QChar('/') );

	if ( pos > 0 ) {
		childid = blkid.right( blkid.length() - pos - 1 );
		blkid = blkid.left( pos );
	}

	for ( QModelIndex iBlock : iBlocks ) {
		if ( nif->blockInherits( iBlock, blkid ) ) {
			if ( childid.isEmpty() )
				return iBlock;

			return nif->getIndex( iBlock, childid );
		}
	}
	return QModelIndex();
}

bool NifSkopeOpenGLContext::ConditionSingle::eval( const NifModel * nif, const QVector<QModelIndex> & iBlocks ) const
{
	if ( left == "BSVersion" )
		return compare( nif->getBSVersion(), right.toUInt( nullptr, 0 ) ) ^ invert;

	QModelIndex iLeft = getIndex( nif, iBlocks, left );

	if ( !iLeft.isValid() )
		return invert;

	if ( comp == NONE )
		return !invert;

	const NifItem * item = nif->getItem( iLeft );
	if ( !item )
		return false;

	if ( item->isString() )
		return compare( item->getValueAsString(), right ) ^ invert;
	else if ( item->isCount() )
		return compare( item->getCountValue(), right.toULongLong( nullptr, 0 ) ) ^ invert;
	else if ( item->isFloat() )
		return compare( item->getFloatValue(), (float)right.toDouble() ) ^ invert;
	else if ( item->isFileVersion() )
		return compare( item->getFileVersionValue(), right.toUInt( nullptr, 0 ) ) ^ invert;
	else if ( item->valueType() == NifValue::tBSVertexDesc )
		return compare( (uint) item->get<BSVertexDesc>().GetFlags(), right.toUInt( nullptr, 0 ) ) ^ invert;

	return false;
}

bool NifSkopeOpenGLContext::ConditionGroup::eval( const NifModel * nif, const QVector<QModelIndex> & iBlocks ) const
{
	if ( conditions.isEmpty() )
		return true;

	if ( isOrGroup() ) {
		for ( Condition * cond : conditions ) {
			if ( cond->eval( nif, iBlocks ) )
				return true;
		}
		return false;
	} else {
		for ( Condition * cond : conditions ) {
			if ( !cond->eval( nif, iBlocks ) )
				return false;
		}
		return true;
	}
}

void NifSkopeOpenGLContext::ConditionGroup::addCondition( Condition * c )
{
	conditions.append( c );
}

NifSkopeOpenGLContext::Shader::Shader( const QString & n, unsigned int t, QOpenGLFunctions_4_1_Core * fn )
	: f( fn ), name( n ), id( 0 ), status( false ), type( t )
{
	id = f->glCreateShader( type );
}

NifSkopeOpenGLContext::Shader::~Shader()
{
	if ( id )
		f->glDeleteShader( id );
}

bool NifSkopeOpenGLContext::Shader::load( const QString & filepath )
{
	try
	{
		QFile file( filepath );

		if ( !file.open( QIODevice::ReadOnly ) )
			throw QString( "couldn't open %1 for read access" ).arg( filepath );

		QByteArray data = file.readAll();
		int	n = data.indexOf( "SF_NUM_TEXTURE_UNITS" );
		if ( n >= 0 )
			data.replace( n, 20, QByteArray::number( TexCache::num_texture_units - 2 ) );

		const char * src = data.constData();

		f->glShaderSource( id, 1, &src, 0 );
		f->glCompileShader( id );

		GLint result;
		f->glGetShaderiv( id, GL_COMPILE_STATUS, &result );

		if ( result != GL_TRUE ) {
			GLint logLen;
			f->glGetShaderiv( id, GL_INFO_LOG_LENGTH, &logLen );
			char * log = new char[ logLen ];
			f->glGetShaderInfoLog( id, logLen, 0, log );
			QString errlog( log );
			delete[] log;
			throw errlog;
		}
	}
	catch ( QString & err )
	{
		status = false;
		Message::append( QObject::tr( "There were errors during shader compilation" ), QString( "%1:\r\n\r\n%2" ).arg( name ).arg( err ) );
		return false;
	}
	status = true;
	return true;
}


NifSkopeOpenGLContext::Program::Program( const QString & n, QOpenGLFunctions_4_1_Core * fn )
	: f( fn ), name( n.toLower() ), id( 0 )
{
	unsigned int	m = ( name.startsWith( QLatin1StringView("stf_") ) ? 512 : 128 );
	uniLocationsMap = new UniformLocationMapItem[m];
	uniLocationsMapMask = m - 1;
	uniLocationsMapSize = 0;
	id = f->glCreateProgram();
}

NifSkopeOpenGLContext::Program::~Program()
{
	if ( id )
		f->glDeleteShader( id );
	delete[] uniLocationsMap;
}

bool NifSkopeOpenGLContext::Program::load( const QString & filepath, NifSkopeOpenGLContext * context )
{
	try
	{
		QFile file( filepath );

		if ( !file.open( QIODevice::ReadOnly ) )
			throw QString( "couldn't open %1 for read access" ).arg( filepath );

		QTextStream stream( &file );

		QStack<ConditionGroup *> chkgrps;
		chkgrps.push( &conditions );

		while ( !stream.atEnd() ) {
			QString line = stream.readLine().trimmed();

			if ( line.startsWith( "shaders" ) ) {
				QStringList list = line.simplified().split( " " );

				for ( int i = 1; i < list.count(); i++ ) {
					Shader * shader = context->shaders.value( list[ i ] );

					if ( shader ) {
						if ( shader->status )
							f->glAttachShader( id, shader->id );
						else
							throw QString( "depends on shader %1 which was not compiled successful" ).arg( list[ i ] );
					} else {
						throw QString( "shader %1 not found" ).arg( list[ i ] );
					}
				}
			} else if ( line.startsWith( "checkgroup" ) ) {
				QStringList list = line.simplified().split( " " );

				if ( list.value( 1 ) == "begin" ) {
					ConditionGroup * group = new ConditionGroup( list.value( 2 ) == "or" );
					chkgrps.top()->addCondition( group );
					chkgrps.push( group );
				} else if ( list.value( 1 ) == "end" ) {
					if ( chkgrps.count() > 1 )
						chkgrps.pop();
					else
						throw QString( "mismatching checkgroup end tag" );
				} else {
					throw QString( "expected begin or end after checkgroup" );
				}
			} else if ( line.startsWith( "check" ) ) {
				line = line.remove( 0, 5 ).trimmed();

				bool invert = false;

				if ( line.startsWith( "not " ) ) {
					invert = true;
					line = line.remove( 0, 4 ).trimmed();
				}

				chkgrps.top()->addCondition( new ConditionSingle( line, invert ) );
			}
		}

		f->glLinkProgram( id );

		GLint result;

		f->glGetProgramiv( id, GL_LINK_STATUS, &result );

		if ( result != GL_TRUE ) {
			GLint logLen = 0;
			f->glGetProgramiv( id, GL_INFO_LOG_LENGTH, &logLen );

			if ( logLen != 0 ) {
				char * log = new char[ logLen ];
				f->glGetProgramInfoLog( id, logLen, 0, log );
				QString errlog( log );
				delete[] log;
				id = 0;
				throw errlog;
			}
		}
	}
	catch ( QString & x )
	{
		status = false;
		Message::append( QObject::tr( "There were errors during shader compilation" ), QString( "%1:\r\n\r\n%2" ).arg( name ).arg( x ) );
		return false;
	}
	status = true;
	return true;
}

inline NifSkopeOpenGLContext::Program::UniformLocationMapItem::UniformLocationMapItem( const char *s, int argsX16Y16 )
	: fmt( s ), args( std::uint32_t(argsX16Y16) ), l( -1 )
{
}

inline bool NifSkopeOpenGLContext::Program::UniformLocationMapItem::operator==( const UniformLocationMapItem & r ) const
{
	return ( fmt == r.fmt && args == r.args );
}

inline std::uint32_t NifSkopeOpenGLContext::Program::UniformLocationMapItem::hashFunction() const
{
	// note: this requires fmt to point to a string literal
	std::uint64_t	tmp = reinterpret_cast< std::uintptr_t >( fmt ) ^ ( std::uint64_t( args ) << 32 );
	std::uint32_t	h = 0xFFFFFFFFU;
	hashFunctionCRC32C< std::uint64_t >( h, tmp );
	return h;
}

int NifSkopeOpenGLContext::Program::storeUniformLocation( const UniformLocationMapItem & o, size_t i )
{
	const char *	fmt = o.fmt;
	int	arg1 = int( o.args & 0xFFFF );
	int	arg2 = int( o.args >> 16 );

	char	varNameBuf[256];
	char *	sp = varNameBuf;
	char *	endp = sp + 254;
	while ( sp < endp ) [[likely]] {
		char	c = *( fmt++ );
		if ( (unsigned char) c > (unsigned char) '%' ) [[likely]] {
			*( sp++ ) = c;
			continue;
		}
		if ( !c )
			break;
		if ( c == '%' ) [[likely]] {
			c = *( fmt++ );
			if ( c == 'd' ) {
				int	n = arg1;
				arg1 = arg2;
				if ( n >= 10 ) {
					c = char( (n / 10) & 15 ) | '0';
					*( sp++ ) = c;
					n = n % 10;
				}
				c = char( n & 15 ) | '0';
			} else if ( c != '%' ) {
				break;
			}
		}
		*( sp++ ) = c;
	}
	*sp = '\0';
	int	l = f->glGetUniformLocation( id, varNameBuf );
	uniLocationsMap[i] = o;
	uniLocationsMap[i].l = l;
	if ( l < 0 )
		std::fprintf( stderr, "[Warning] Uniform '%s' not found\n", varNameBuf );

	uniLocationsMapSize++;
	if ( ( uniLocationsMapSize * size_t(3) ) > ( uniLocationsMapMask * size_t(2) ) ) {
		unsigned int	m = ( uniLocationsMapMask << 1 ) | 0xFFU;
		UniformLocationMapItem *	tmpBuf = new UniformLocationMapItem[m + 1U];
		for ( size_t j = 0; j <= uniLocationsMapMask; j++ ) {
			size_t	k = uniLocationsMap[j].hashFunction() & m;
			while ( tmpBuf[k].fmt )
				k = ( k + 1 ) & m;
			tmpBuf[k] = uniLocationsMap[j];
		}
		delete[] uniLocationsMap;
		uniLocationsMap = tmpBuf;
		uniLocationsMapMask = m;
	}

	return l;
}

int NifSkopeOpenGLContext::Program::uniLocation( const char * fmt )
{
	UniformLocationMapItem	key( fmt, 0 );

	size_t	hashMask = uniLocationsMapMask;
	size_t	i = key.hashFunction() & hashMask;
	for ( ; uniLocationsMap[i].fmt; i = (i + 1) & hashMask ) {
		if ( uniLocationsMap[i] == key )
			return uniLocationsMap[i].l;
	}

	return storeUniformLocation( key, i );
}

int NifSkopeOpenGLContext::Program::uniLocation( const char * fmt, int argsX16Y16 )
{
	UniformLocationMapItem	key( fmt, argsX16Y16 );

	size_t	hashMask = uniLocationsMapMask;
	size_t	i = key.hashFunction() & hashMask;
	for ( ; uniLocationsMap[i].fmt; i = (i + 1) & hashMask ) {
		if ( uniLocationsMap[i] == key )
			return uniLocationsMap[i].l;
	}

	return storeUniformLocation( key, i );
}

void NifSkopeOpenGLContext::Program::uni1i( const char * name, int x )
{
	UniformLocationMapItem	key( name, 0 );

	size_t	hashMask = uniLocationsMapMask;
	size_t	i = key.hashFunction() & hashMask;
	for ( ; uniLocationsMap[i].fmt; i = (i + 1) & hashMask ) {
		if ( uniLocationsMap[i] == key ) {
			f->glUniform1i( uniLocationsMap[i].l, x );
			return;
		}
	}

	f->glUniform1i( storeUniformLocation( key, i ), x );
}

void NifSkopeOpenGLContext::Program::uni1f( const char * name, float x )
{
	UniformLocationMapItem	key( name, 0 );

	size_t	hashMask = uniLocationsMapMask;
	size_t	i = key.hashFunction() & hashMask;
	for ( ; uniLocationsMap[i].fmt; i = (i + 1) & hashMask ) {
		if ( uniLocationsMap[i] == key ) {
			f->glUniform1f( uniLocationsMap[i].l, x );
			return;
		}
	}

	f->glUniform1f( storeUniformLocation( key, i ), x );
}

void NifSkopeOpenGLContext::Program::uni1b_l( int l, bool x )
{
	f->glUniform1i( l, int(x) );
}

void NifSkopeOpenGLContext::Program::uni1i_l( int l, int x )
{
	f->glUniform1i( l, x );
}

void NifSkopeOpenGLContext::Program::uni1f_l( int l, float x )
{
	f->glUniform1f( l, x );
}

void NifSkopeOpenGLContext::Program::uni2f_l( int l, float x, float y )
{
	f->glUniform2f( l, x, y );
}

void NifSkopeOpenGLContext::Program::uni3f_l( int l, float x, float y, float z )
{
	f->glUniform3f( l, x, y, z );
}

void NifSkopeOpenGLContext::Program::uni4f_l( int l, FloatVector4 x )
{
	f->glUniform4f( l, x[0], x[1], x[2], x[3] );
}

void NifSkopeOpenGLContext::Program::uni4srgb_l( int l, FloatVector4 x )
{
	x = DDSTexture16::srgbExpand( x );
	f->glUniform4f( l, x[0], x[1], x[2], x[3] );
}

void NifSkopeOpenGLContext::Program::uni4c_l( int l, std::uint32_t c, bool isSRGB )
{
	FloatVector4	x(c);
	x *= 1.0f / 255.0f;
	if ( isSRGB )
		x = DDSTexture16::srgbExpand( x );
	f->glUniform4f( l, x[0], x[1], x[2], x[3] );
}

void NifSkopeOpenGLContext::Program::uni1bv_l( int l, const bool * x, size_t n )
{
	n = std::min< size_t >( n, 64 );
	GLint	tmp[64];
	for ( size_t i = 0; i < n; i++ )
		tmp[i] = GLint( x[i] );
	f->glUniform1iv( l, GLsizei(n), tmp );
}

void NifSkopeOpenGLContext::Program::uni1iv_l( int l, const int * x, size_t n )
{
	f->glUniform1iv( l, GLsizei(n), x );
}

void NifSkopeOpenGLContext::Program::uni1fv_l( int l, const float * x, size_t n )
{
	f->glUniform1fv( l, GLsizei(n), x );
}

void NifSkopeOpenGLContext::Program::uni4fv_l( int l, const FloatVector4 * x, size_t n )
{
	f->glUniform4fv( l, GLsizei(n), &(x[0][0]) );
}

void NifSkopeOpenGLContext::Program::uni3m_l( int l, const Matrix & val )
{
	f->glUniformMatrix3fv( l, 1, 0, val.data() );
}

void NifSkopeOpenGLContext::Program::uni4m_l( int l, const Matrix4 & val )
{
	f->glUniformMatrix4fv( l, 1, 0, val.data() );
}

void NifSkopeOpenGLContext::Program::uniSampler_l( int l, int firstTextureUnit, int textureCnt, int arraySize )
{
	arraySize = std::min< int >( arraySize, TexCache::maxTextureUnits );
	textureCnt = std::min< int >( textureCnt, arraySize );
	GLint	tmp[TexCache::maxTextureUnits];
	int	i;
	for ( i = 0; i < textureCnt; i++ )
		tmp[i] = firstTextureUnit + i;
	for ( ; i < arraySize; i++ )
		tmp[i] = firstTextureUnit;
	f->glUniform1iv( l, arraySize, tmp );
}

bool NifSkopeOpenGLContext::Program::uniSampler( BSShaderLightingProperty * bsprop, const char * var,
									int textureSlot, int & texunit, const QString & alternate,
									uint clamp, const QString & forced )
{
	int	uniSamp = uniLocation( var );
	if ( uniSamp < 0 )
		return true;
	if ( !activateTextureUnit( f, texunit ) )
		return false;

	// TODO: On stream 155 bsprop->fileName can reference incorrect strings because
	// the BSSTS is not filled out nor linked from the BSSP
	do {
		if ( !forced.isEmpty() && bsprop->bind( forced, true, TexClampMode(clamp) ) )
			break;
		if ( textureSlot >= 0 ) {
			QString	fname = bsprop->fileName( textureSlot );
			if ( !fname.isEmpty() && bsprop->bind( fname, false, TexClampMode(clamp) ) )
				break;
		}
		if ( !alternate.isEmpty() && bsprop->bind( alternate, false, TexClampMode::WRAP_S_WRAP_T ) )
			break;
		const QString *	fname = &black;
		if ( textureSlot == 0 )
			fname = &white;
		else if ( textureSlot == 1 )
			fname = ( bsprop->bsVersion < 151 ? &default_n : &default_ns );
		else if ( textureSlot >= 8 && bsprop->bsVersion >= 151 )
			fname = ( textureSlot == 8 ? &reflectivity : &lighting );
		if ( bsprop->bind( *fname, true, TexClampMode::WRAP_S_WRAP_T ) )
			break;

		return false;
	} while ( false );

	f->glUniform1i( uniSamp, texunit++ );
	return true;
}


NifSkopeOpenGLContext::NifSkopeOpenGLContext( QOpenGLContext * context )
	:	fn( QOpenGLVersionFunctionsFactory::get< QOpenGLFunctions_4_1_Core >( context ) ), cx( context ),
		lightSourcePosition0( 0.0f, 0.0f, 1.0f, 0.0f ), lightSourceDiffuse0( 1.0f ), lightSourceAmbient( 1.0f ),
		lightSourcePosition1( 0.0f, 0.0f, 1.0f, 0.0f ), lightSourceDiffuse1( 1.0f ),
		lightSourcePosition2( 0.0f, 0.0f, 1.0f, 0.0f ), lightSourceDiffuse2( 1.0f )
{
	vertexAttrib1f = reinterpret_cast< void (*)( unsigned int, float ) >( cx->getProcAddress( "glVertexAttrib1f" ) );
	vertexAttrib2fv =
		reinterpret_cast< void (*)( unsigned int, const float * ) >( cx->getProcAddress( "glVertexAttrib2fv" ) );
	vertexAttrib3fv =
		reinterpret_cast< void (*)( unsigned int, const float * ) >( cx->getProcAddress( "glVertexAttrib3fv" ) );
	vertexAttrib4fv =
		reinterpret_cast< void (*)( unsigned int, const float * ) >( cx->getProcAddress( "glVertexAttrib4fv" ) );
	if ( !( fn && vertexAttrib1f && vertexAttrib2fv && vertexAttrib3fv && vertexAttrib4fv ) )
		throw NifSkopeError( "failed to initialize OpenGL functions" );
}

NifSkopeOpenGLContext::~NifSkopeOpenGLContext()
{
	flushCache();
	releaseShaders();
}

void NifSkopeOpenGLContext::updateShaders()
{
	releaseShaders();

	QDir dir( QCoreApplication::applicationDirPath() );

	if ( dir.exists( "shaders" ) )
		dir.cd( "shaders" );

#ifdef Q_OS_LINUX
	else if ( dir.exists( "/usr/share/nifskope/shaders" ) )
		dir.cd( "/usr/share/nifskope/shaders" );
#endif

	dir.setNameFilters( { "*.vert" } );
	for ( const QString& name : dir.entryList() ) {
		Shader * shader = new Shader( name, GL_VERTEX_SHADER, fn );
		shader->load( dir.filePath( name ) );
		shaders.insert( name, shader );
	}

	dir.setNameFilters( { "*.frag" } );
	for ( const QString& name : dir.entryList() ) {
		Shader * shader = new Shader( name, GL_FRAGMENT_SHADER, fn );
		shader->load( dir.filePath( name ) );
		shaders.insert( name, shader );
	}

	dir.setNameFilters( { "*.prog" } );
	for ( const QString& name : dir.entryList() ) {
		Program * program = new Program( name, fn );
		program->load( dir.filePath( name ), this );
		programs.insert( program->name, program );
	}
}

void NifSkopeOpenGLContext::releaseShaders()
{
	qDeleteAll( programs );
	programs.clear();
	qDeleteAll( shaders );
	shaders.clear();
}

NifSkopeOpenGLContext::Program * NifSkopeOpenGLContext::useProgram( const QString & name )
{
	Program *	prog = programs.value( name );
	unsigned int	id = 0;
	if ( prog && prog->status )
		id = prog->id;
	fn->glUseProgram( id );
	return prog;
}

void NifSkopeOpenGLContext::stopProgram()
{
	fn->glUseProgram( 0 );
}

void NifSkopeOpenGLContext::setGlobalUniforms()
{
	for ( Program * p : programs ) {
		if ( !p->status )
			continue;
		fn->glUseProgram( p->id );
		p->uni3m( "viewMatrix", viewMatrix );
		p->uni4m( "projectionMatrix", projectionMatrix );
		p->uni4f( "lightSourcePosition0", lightSourcePosition0 );
		p->uni4f( "lightSourceDiffuse0", lightSourceDiffuse0 );
		p->uni4f( "lightSourceAmbient", lightSourceAmbient );
		p->uni4f( "lightSourcePosition1", lightSourcePosition1 );
		p->uni4f( "lightSourceDiffuse1", lightSourceDiffuse1 );
		p->uni4f( "lightSourcePosition2", lightSourcePosition2 );
		p->uni4f( "lightSourceDiffuse2", lightSourceDiffuse2 );
	}
	fn->glUseProgram( 0 );
}

void NifSkopeOpenGLContext::drawShape(
	unsigned int numVerts, std::uint64_t attrMask,
	unsigned int numIndices, unsigned int elementMode, unsigned int elementType,
	const float * const * attrData, const void * elementData )
{
	size_t	elementDataSize = ( elementType == GL_UNSIGNED_SHORT ? 2 : ( elementType == GL_UNSIGNED_INT ? 4 : 1 ) );
	elementDataSize = elementDataSize * numIndices;
	ShapeDataHash	h( numVerts, attrMask, elementDataSize, attrData, elementData );
	drawShape( h, numIndices, elementMode, elementType, attrData, elementData );
}

void NifSkopeOpenGLContext::drawShape(
	const ShapeDataHash & h, unsigned int numIndices, unsigned int elementMode, unsigned int elementType,
	const float * const * attrData, const void * elementData )
{
	if ( ( cacheShapeCnt * 3U ) >= ( geometryCache.size() * 2U ) ) [[unlikely]]
		rehashCache();

	ShapeData *	d = nullptr;
	std::uint32_t	m = std::uint32_t( geometryCache.size() - 1 );
	std::uint32_t	i = h.hashFunction() & m;
	for ( ; geometryCache[i]; i = ( i + 1 ) & m ) {
		if ( geometryCache[i]->h == h ) {
			d = geometryCache[i];
			break;
		}
	}

	QOpenGLFunctions_4_1_Core &	f = *fn;
	if ( d ) {
		if ( d != cacheLastItem ) {
			d->prev->next = d->next;
			d->next->prev = d->prev;
			d->prev = cacheLastItem;
			d->next = cacheLastItem->next;
			d->prev->next = d;
			d->next->prev = d;
		}
		cacheLastItem = d;
		f.glBindVertexArray( d->vao );
		f.glDrawElements( GLenum( elementMode ), GLsizei( numIndices ), GLenum( elementType ), (void *) 0 );
		return;
	}

	d = new ShapeData( *this, h, attrData, elementData );
	if ( !cacheLastItem ) {
		d->prev = d;
		d->next = d;
	} else {
		d->prev = cacheLastItem;
		d->next = cacheLastItem->next;
		d->prev->next = d;
		d->next->prev = d;
	}
	cacheLastItem = d;
	geometryCache[i] = d;
	cacheShapeCnt++;
	auto	bufferCountAndSize = d->h.getBufferCountAndSize();
	cacheBufferCnt += bufferCountAndSize.first;
	cacheBytesUsed += bufferCountAndSize.second;

	f.glDrawElements( GLenum( elementMode ), GLsizei( numIndices ), GLenum( elementType ), (void *) 0 );
}

void NifSkopeOpenGLContext::setCacheLimits( size_t maxShapes, size_t maxBuffers, size_t maxBytes )
{
	cacheMaxShapes = std::uint32_t( maxShapes );
	cacheMaxBuffers = std::uint32_t( maxBuffers );
	cacheMaxBytes = std::uint32_t( maxBytes );
}

void NifSkopeOpenGLContext::shrinkCache( bool deleteAll )
{
	bool	rehashNeeded = false;

	while ( cacheLastItem ) {
		ShapeData *	d = cacheLastItem->next;

		if ( deleteAll ) {
			cacheShapeCnt = 0;
			cacheBufferCnt = 0;
			cacheBytesUsed = 0;
		} else {
			if ( cacheShapeCnt < cacheMaxShapes && cacheBufferCnt < cacheMaxBuffers && cacheBytesUsed < cacheMaxBytes )
				break;
			cacheShapeCnt--;
			auto	bufferCountAndSize = d->h.getBufferCountAndSize();
			cacheBufferCnt -= bufferCountAndSize.first;
			cacheBytesUsed -= bufferCountAndSize.second;
		}

		if ( !rehashNeeded ) {
			geometryCache.clear();
			rehashNeeded = true;
		}
		if ( d->prev == d ) {
			cacheLastItem = nullptr;
		} else {
			d->prev->next = d->next;
			d->next->prev = d->prev;
		}
		delete d;
	}

	if ( rehashNeeded )
		rehashCache();
}

void NifSkopeOpenGLContext::rehashCache()
{
	size_t	n = 256UL << std::bit_width( ( cacheShapeCnt * 3U ) >> 9U );
	if ( geometryCache.size() == n )
		return;

	std::uint32_t	m = std::uint32_t( n - 1 );
	geometryCache.clear();
	geometryCache.resize( n, nullptr );
	ShapeData *	d = cacheLastItem;
	if ( !d )
		return;
	do {
		std::uint32_t	i = d->h.hashFunction() & m;
		while ( geometryCache[i] )
			i = ( i + 1 ) & m;
		geometryCache[i] = d;
		d = d->prev;
	} while ( d != cacheLastItem );
}


inline std::uint32_t NifSkopeOpenGLContext::ShapeDataHash::hashFunction() const
{
	std::uint32_t	tmp = 0xFFFFFFFFU;
	hashFunctionCRC32C< std::uint64_t >( tmp, attrMask );
	hashFunctionCRC32C< std::uint64_t >( tmp, h[0] );
	hashFunctionCRC32C< std::uint64_t >( tmp, h[1] );
	return tmp;
}

std::pair< std::uint32_t, std::uint32_t > NifSkopeOpenGLContext::ShapeDataHash::getBufferCountAndSize() const
{
	std::uint64_t	tmp = ( ~attrMask >> 3 ) & 0x1111111111111111ULL;
	tmp = ( tmp * 7U ) & attrMask;
	std::uint64_t	tmp2 = ( tmp | ( tmp >> 1 ) | ( tmp >> 2 ) ) & 0x1111111111111111ULL;
	std::uint32_t	numBuffers = std::uint32_t( std::popcount( tmp2 ) + 1 );

	tmp = ( tmp + ( tmp >> 4 ) ) & 0x0F0F0F0F0F0F0F0FULL;
	tmp = tmp + ( tmp >> 8 );
	tmp = tmp + ( tmp >> 16 );
	tmp = tmp + ( tmp >> 32 );
	std::uint32_t	totalDataSize = std::uint32_t( ( tmp & 0xFFU ) * sizeof( float ) * numVerts + elementBytes );

	return std::pair< std::uint32_t, std::uint32_t >( numBuffers, totalDataSize );
}


NifSkopeOpenGLContext::ShapeData::ShapeData(
	NifSkopeOpenGLContext & context, const ShapeDataHash & dataHash,
	const float * const * attrData, const void * elementData )
	: h( dataHash ), prev( nullptr ), next( nullptr ), fn( context.fn ), vao( 0 ), ebo( 0 )
{
	std::uint64_t	attrMask = dataHash.attrMask;
	std::uint32_t	vertCnt = dataHash.numVerts;
	std::uint32_t	elementDataSize = dataHash.elementBytes;

	QOpenGLFunctions_4_1_Core &	f = *( context.fn );
	f.glGenVertexArrays( 1, &vao );
	f.glBindVertexArray( vao );
	for ( size_t i = 0; attrMask; i++, attrMask = attrMask >> 4 ) {
		size_t	nBytes = attrMask & 7;
		if ( !nBytes )
			continue;
		nBytes = nBytes * sizeof( float );
		if ( attrMask & 8 ) {
			f.glDisableVertexAttribArray( GLuint( i ) );
			if ( ( attrMask & 7 ) >= 4 )
				context.vertexAttrib4fv( GLuint( i ), attrData[i] );
			else if ( ( attrMask & 7 ) == 3 )
				context.vertexAttrib3fv( GLuint( i ), attrData[i] );
			else if ( ( attrMask & 7 ) == 2 )
				context.vertexAttrib2fv( GLuint( i ), attrData[i] );
			else
				context.vertexAttrib1f( GLuint( i ), attrData[i][0] );
		} else {
			nBytes = nBytes * vertCnt;
			f.glGenBuffers( 1, &( vbo[i] ) );
			f.glBindBuffer( GL_ARRAY_BUFFER, vbo[i] );
			f.glBufferData( GL_ARRAY_BUFFER, GLsizeiptr( nBytes ), attrData[i], GL_STATIC_DRAW );
			f.glVertexAttribPointer( GLuint( i ), GLint( attrMask & 7 ), GL_FLOAT, GL_FALSE, 0, (void *) 0 );
			f.glEnableVertexAttribArray( GLuint( i ) );
		}
	}

	f.glGenBuffers( 1, &ebo );
	f.glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, ebo );
	f.glBufferData( GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr( elementDataSize ), elementData, GL_STATIC_DRAW );
}

NifSkopeOpenGLContext::ShapeData::~ShapeData()
{
	QOpenGLFunctions_4_1_Core &	f = *fn;
	f.glBindVertexArray( 0 );
	f.glDeleteVertexArrays( 1, &vao );
	f.glDeleteBuffers( 1, &ebo );
	std::uint64_t	m = h.attrMask;
	for ( size_t i = 0; m; i++, m = m >> 4 ) {
		if ( ( m & 7 ) && !( m & 8 ) )
			f.glDeleteBuffers( 1, &( vbo[i] ) );
	}
}


#define XXH_INLINE_ALL 1
#include "xxhash.h"

NifSkopeOpenGLContext::ShapeDataHash::ShapeDataHash(
	std::uint32_t vertCnt, std::uint64_t attrModeMask, size_t elementDataSize,
	const float * const * attrData, const void * elementData )
	: attrMask( attrModeMask ), numVerts( vertCnt ), elementBytes( std::uint32_t(elementDataSize) )
{
	XXH3_state_t	xxhState;
	XXH3_128bits_reset( &xxhState );
	for ( std::uint32_t i = 0; true; i++, attrModeMask = attrModeMask >> 4 ) {
		const void *	p;
		size_t	nBytes;
		if ( !attrModeMask ) {
			nBytes = elementDataSize;
			p = elementData;
		} else if ( ( nBytes = attrModeMask & 7 ) != 0 ) {
			nBytes = nBytes * sizeof( float );
			if ( !( attrModeMask & 8 ) )
				nBytes = nBytes * vertCnt;
			p = attrData[i];
		}
		if ( nBytes > 0 )
			XXH3_128bits_update( &xxhState, p, nBytes );
		if ( !attrModeMask )
			break;
	}
	XXH128_hash_t	xxhResult = XXH3_128bits_digest( &xxhState );
	h[0] = xxhResult.low64;
	h[1] = xxhResult.high64;
}

