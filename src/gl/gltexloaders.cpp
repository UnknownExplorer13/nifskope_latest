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

#include "gltex.h"

#include "message.h"
#include "model/nifmodel.h"

#include "libfo76utils/src/filebuf.hpp"
#include "libfo76utils/src/pbr_lut.hpp"
#include "libfo76utils/src/sfcube2.hpp"

#include <gli.hpp>

#include <QBuffer>
#include <QByteArray>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QModelIndex>
#include <QOpenGLContext>
#include <QString>
#include <QtEndian>

#include "dds.h"

/*! @file gltexloaders.cpp
 * @brief Texture loading functions.
 *
 * Textures can be loaded from a filename (in any supported format) or raw
 * pixel data. They can also be exported from raw pixel data to TGA or DDS.
 *
 * Supported read formats:
 * - DDS (RAW, DXTn, 8-bit palette)
 * - TGA
 * - BMP
 * - NIF (RGB8, RGBA8, PAL8, DXT1, DXT5)
 *
 * Supported write formats:
 * - TGA (32-bit) from NIF
 * - DDS (RGB, RGBA, DXT1, DXT5) from NIF
 * - NIF from NIF (matching versions only, experimental)
 *
 */

#define FOURCC_DXT1 MAKEFOURCC( 'D', 'X', 'T', '1' )
#define FOURCC_DXT3 MAKEFOURCC( 'D', 'X', 'T', '3' )
#define FOURCC_DXT5 MAKEFOURCC( 'D', 'X', 'T', '5' )

//! Shift amounts for RGBA conversion
static const int rgbashift[4] = {
	0, 8, 16, 24
};
//! Mask for TGA greyscale
static const quint32 TGA_L_MASK[4] = {
	0xff, 0xff, 0xff, 0x00
};
//! Mask for TGA greyscale with alpha
static const quint32 TGA_LA_MASK[4] = {
	0x00ff, 0x00ff, 0x00ff, 0xff00
};
//! Mask for TGA RGBA
static const quint32 TGA_RGBA_MASK[4] = {
	0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000
};
//! Mask for TGA RGB
static const quint32 TGA_RGB_MASK[4] = {
	0x00ff0000, 0x0000ff00, 0x000000ff, 0x00000000
};
//! Mask for BMP RGBA (identical to TGA RGB)
static const quint32 BMP_RGBA_MASK[4] = {
	0x00ff0000, 0x0000ff00, 0x000000ff, 0x00000000
};
//! Inverse mask for RGBA
static const quint32 RGBA_INV_MASK[4] = {
	0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000
};

//! Unknown 8 bytes for 24-bit NiPixelData
static const quint8 unk8bytes24[] = {
	96, 8, 130, 0, 0, 65, 0, 0
};
//! Unknown 8 bytes for 32-bit NiPixelData
static const quint8 unk8bytes32[] = {
	129, 8, 130, 32, 0, 65, 12, 0
};

//! Check whether a number is a power of two.
static inline bool isPowerOfTwo( unsigned int x )
{
	return ( x && !( x & ( x - 1U ) ) );
}

/*! Completes mipmap sequence of the current active OpenGL texture.
 *
 * @param m Number of mipmaps that are already in the texture.
 * @return	Total number of mipmaps.
 */
static int generateMipMaps( int m )
{
	GLint w = 0, h = 0;

	// load the (m-1)'th mipmap as a basis
	glGetTexLevelParameteriv( GL_TEXTURE_2D, m - 1, GL_TEXTURE_WIDTH, &w );
	glGetTexLevelParameteriv( GL_TEXTURE_2D, m - 1, GL_TEXTURE_HEIGHT, &h );

	//qDebug() << m-1 << w << h;

	std::vector< std::uint32_t >	data( size_t(w) * size_t(h) );
	glGetTexImage( GL_TEXTURE_2D, m - 1, GL_RGBA, GL_UNSIGNED_BYTE, data.data() );

	// now generate the mipmaps until width is one and height is one.
	while ( w > 1 || h > 1 ) {
		// the buffer overwrites itself to save memory
		std::uint32_t * dst = data.data();
		const std::uint32_t * src = dst;

		quint32 xo = ( w > 1 ? 1 : 0 );
		quint32 yo = ( h > 1 ? w : 0 );

		w /= 2;
		h /= 2;

		if ( w == 0 )
			w = 1;

		if ( h == 0 )
			h = 1;

		//qDebug() << m << w << h;

		for ( int y = 0; y < h; y++ ) {
			for ( int x = 0; x < w; x++, dst++ ) {
				FloatVector4	tmp = ( FloatVector4( src ) + FloatVector4( src + xo )
										+ FloatVector4( src + yo ) + FloatVector4( src + xo + yo ) ) * 0.25f;
				*dst = std::uint32_t( tmp );

				src += xo + 1;
			}

			src += yo;
		}

		glTexImage2D( GL_TEXTURE_2D, m++, 4, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data.data() );
	}

	return m;
}

/*! Converts RLE-encoded data into pixel data.
 *
 * TGA in particular uses the PackBits format described at
 * http://en.wikipedia.org/wiki/PackBits and in the TGA spec.
 */
static bool uncompressRLE( QIODevice & f, int w, int h, int bytespp, quint8 * pixel )
{
	QByteArray data = f.readAll();

	int c = 0; // total pixel count
	int o = 0; // data offset

	quint8 rl; // runlength - 1

	while ( c < w * h ) {
		rl = data[o++];

		if ( rl & 0x80 ) {
			// if RLE packet
			quint8 px[8] = {}; // pixel data in this packet (assume bytespp < 8)

			for ( int b = 0; b < bytespp; b++ )
				px[b] = data[o++];

			rl &= 0x7f; // strip RLE bit

			do {
				for ( int b = 0; b < bytespp; b++ )
					*pixel++ = px[b]; // expand pixel data (rl+1) times
			} while ( ++c < w * h && rl-- > 0 );
		} else {
			do {
				for ( int b = 0; b < bytespp; b++ )
					*pixel++ = data[o++]; // write (rl+1) raw pixels
			} while ( ++c < w * h && rl-- > 0 );
		}

		if ( o >= data.size() )
			return false;
	}

	return true;
}

/*! Convert pixels to RGBA
 *
 * @param data		Pixels to convert
 * @param w			Width of the image
 * @param h			Height of the image
 * @param bytespp	Number of bytes per pixel
 * @param mask		Bitmask for pixel data
 * @param flipV		Whether to flip the data vertically
 * @param flipH		Whether to flip the data horizontally
 * @param pixl		Pixels to output
 */
static void convertToRGBA( const quint8 * data, int w, int h, int bytespp,
							const quint32 mask[], bool flipV, bool flipH, quint8 * pixl )
{
	std::memset( pixl, 0, w * h * 4 );

	for ( int a = 0; a < 4; a++ ) {
		if ( mask[a] ) {
			quint32 msk = mask[ a ];
			int rshift  = 0;

			while ( msk != 0 && ( msk & 0xffffff00 ) ) {
				msk = msk >> 1; rshift++;
			}

			int lshift = rgbashift[ a ];

			while ( msk != 0 && ( ( msk & 0x80 ) == 0 ) ) {
				msk = msk << 1; lshift++;
			}

			msk = mask[ a ];

			const quint8 * src = data;
			const quint32 inc  = ( flipH ? -1 : 1 );

			for ( int y = 0; y < h; y++ ) {
				quint32 * dst = (quint32 *)( pixl + 4 * ( w * ( flipV ? h - y - 1 : y ) + ( flipH ? w - 1 : 0 ) ) );

				if ( rshift == lshift ) {
					for ( int x = 0; x < w; x++ ) {
						*dst |= *( (const quint32 *)src ) & msk;
						dst  += inc;
						src  += bytespp;
					}
				} else {
					for ( int x = 0; x < w; x++ ) {
						*dst |= ( *( (const quint32 *)src ) & msk ) >> rshift << lshift;
						dst  += inc;
						src  += bytespp;
					}
				}
			}
		} else if ( a == 3 ) {
			quint32 * dst = (quint32 *)pixl;
			quint32 x = 0xff << rgbashift[ a ];

			for ( int c = w * h; c > 0; c-- )
				*dst++ |= x;
		}
	}
}

//! Load raw pixel data
static int texLoadRaw( QIODevice & f, int width, int height, int num_mipmaps, int bpp, int bytespp,
						const quint32 mask[], bool flipV = false, bool flipH = false, bool rle = false )
{
	if ( bytespp * 8 != bpp || bpp > 32 || bpp < 8 )
		throw QString( "unsupported image depth %1 / %2" ).arg( bpp ).arg( bytespp );

	glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
	glPixelStorei( GL_UNPACK_SWAP_BYTES, GL_FALSE );

	quint8 * data1 = (quint8 *)malloc( width * height * 4 );
	quint8 * data2 = (quint8 *)malloc( width * height * 4 );

	int w = width;
	int h = height;
	int m = 0;

	while ( m < num_mipmaps ) {
		w = width >> m;
		h = height >> m;

		if ( w == 0 )
			w = 1;

		if ( h == 0 )
			h = 1;

		if ( rle ) {
			if ( !uncompressRLE( f, w, h, bytespp, data1 ) ) {
				free( data2 );
				free( data1 );
				throw QString( "unexpected EOF" );
			}
		} else if ( f.read( (char *)data1, w * h * bytespp ) != w * h * bytespp ) {
			free( data2 );
			free( data1 );
			throw QString( "unexpected EOF" );
		}

		convertToRGBA( data1, w, h, bytespp, mask, flipV, flipH, data2 );

		glTexImage2D( GL_TEXTURE_2D, m++, 4, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data2 );

		if ( w == 1 && h == 1 )
			break;
	}

	free( data2 );
	free( data1 );

	if ( w > 1 || h > 1 )
		m = generateMipMaps( m );

	return m;
}

//! Load a palettised texture
static int texLoadPal( QIODevice & f, int width, int height, int num_mipmaps, int bpp, int bytespp,
						const quint32 colormap[], bool flipV, bool flipH, bool rle )
{
	if ( bpp != 8 || bytespp != 1 )
		throw QString( "unsupported image depth %1 / %2" ).arg( bpp ).arg( bytespp );

	glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
	glPixelStorei( GL_UNPACK_SWAP_BYTES, GL_FALSE );

	quint8 * data = (quint8 *)malloc( width * height * 1 );
	quint8 * pixl = (quint8 *)malloc( width * height * 4 );

	int w = width;
	int h = height;
	int m = 0;

	while ( m < num_mipmaps ) {
		w = width >> m;
		h = height >> m;

		if ( w == 0 )
			w = 1;

		if ( h == 0 )
			h = 1;

		if ( rle ) {
			if ( !uncompressRLE( f, w, h, bytespp, data ) ) {
				free( pixl );
				free( data );
				throw QString( "unexpected EOF" );
			}
		} else if ( f.read( (char *)data, w * h * bytespp ) != w * h * bytespp ) {
			free( pixl );
			free( data );
			throw QString( "unexpected EOF" );
		}

		quint8 * src = data;

		for ( int y = 0; y < h; y++ ) {
			quint32 * dst = (quint32 *)( pixl + 4 * ( w * ( flipV ? h - y - 1 : y ) + ( flipH ? w - 1 : 0 ) ) );

			for ( int x = 0; x < w; x++ ) {
				*dst++ = colormap[*src++];
			}
		}

		glTexImage2D( GL_TEXTURE_2D, m++, 4, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixl );

		if ( w == 1 && h == 1 )
			break;
	}

	free( pixl );
	free( data );

	if ( w > 1 || h > 1 )
		m = generateMipMaps( m );

	return m;
}


// TGA constants
// Note that TGA_X_RLE = TGA_X + 8
// i.e. RLE = hdr[2] & 0x8

#define TGA_COLORMAP     1
#define TGA_COLOR        2
#define TGA_GREY         3
#define TGA_COLORMAP_RLE 9
#define TGA_COLOR_RLE    10
#define TGA_GREY_RLE     11

//! Load a TGA texture.
static GLuint texLoadTGA(
	QIODevice & f, TexCache::TexFmt & texformat, GLenum & target, GLuint & width, GLuint & height, GLuint * id )
{
	// see http://en.wikipedia.org/wiki/Truevision_TGA for a lot of this
	texformat.imageFormat = TexCache::TexFmt::TEXFMT_TGA;
	target = GL_TEXTURE_2D;

	glBindTexture( target, id[0] );

	// read in tga header
	quint8 hdr[18];
	qint64 readBytes = f.read( (char *)hdr, 18 );

	if ( readBytes != 18 )
		throw QString( "unexpected EOF" );

	// ID tag, if present
	if ( hdr[0] )
		f.read( hdr[0] );

	quint8 depth = hdr[16];
	//quint8 alphaDepth  = hdr[17] & 15;
	bool flipV = !( hdr[17] & 32 );
	bool flipH = hdr[17] & 16;
	width  = hdr[12] + 256 * hdr[13];
	height = hdr[14] + 256 * hdr[15];

	if ( !( isPowerOfTwo( width ) && isPowerOfTwo( height ) ) )
		throw QString( "image dimensions must be power of two" );

	quint32 colormap[256];

	if ( hdr[1] ) {
		// color map present
		quint16 offset = hdr[3] + 256 * hdr[4];
		quint16 length = hdr[5] + 256 * hdr[6];
		quint8 bits  = hdr[7];
		quint8 bytes = bits / 8;

		//qDebug() << "COLORMAP" << "offset" << offset << "length" << length << "bits" << bits << "depth" << depth;

		if ( bits != 32 && bits != 24 )
			throw QString( "image sub format not supported" );

		quint32 cnt = offset;
		quint32 col;

		while ( length-- ) {
			col = 0;

			if ( f.read( (char *)&col, bytes ) != bytes )
				throw QString( "unexpected EOF" );

			if ( cnt < 256 ) {
				switch ( bits ) {
				case 24:
					colormap[cnt] = ( ( col & 0x00ff0000 ) >> 16 ) | ( col & 0x0000ff00 ) | ( ( col & 0xff ) << 16 ) | 0xff000000;
					break;
				case 32:
					colormap[cnt] = ( ( col & 0x00ff0000 ) >> 16 ) | ( col & 0xff00ff00 ) | ( ( col & 0xff ) << 16 );
					break;
				}
			}

			cnt++;
		}
	}

	// check format and call texLoadPal / texLoadRaw
	switch ( hdr[2] ) {
	case TGA_COLORMAP:
	case TGA_COLORMAP_RLE:

		if ( depth == 8 && hdr[1] ) {
			texformat.imageEncoding |= TexCache::TexFmt::TEXFMT_PAL8;

			if ( hdr[2] == TGA_COLORMAP_RLE )
				texformat.imageEncoding |= TexCache::TexFmt::TEXFMT_RLE;

			return texLoadPal( f, width, height, 1, depth, depth / 8, colormap, flipV, flipH, hdr[2] == TGA_COLORMAP_RLE );
		}

		break;
	case TGA_GREY:
	case TGA_GREY_RLE:

		if ( depth == 8 ) {
			texformat.imageEncoding |= TexCache::TexFmt::TEXFMT_GRAYSCALE;

			if ( hdr[2] == TGA_GREY_RLE )
				texformat.imageEncoding |= TexCache::TexFmt::TEXFMT_RLE;

			return texLoadRaw( f, width, height, 1, 8, 1, TGA_L_MASK, flipV, flipH, hdr[2] == TGA_GREY_RLE );
		} else if ( depth == 16 ) {
			texformat.imageEncoding |= TexCache::TexFmt::TEXFMT_GRAYSCALE_ALPHA;

			if ( hdr[2] == TGA_GREY_RLE )
				texformat.imageEncoding |= TexCache::TexFmt::TEXFMT_RLE;

			return texLoadRaw( f, width, height, 1, 16, 2, TGA_LA_MASK, flipV, flipH, hdr[2] == TGA_GREY_RLE );
		}

		break;
	case TGA_COLOR:
	case TGA_COLOR_RLE:

		if ( depth == 32 ) {
			texformat.imageEncoding |= TexCache::TexFmt::TEXFMT_RGBA8;

			if ( hdr[2] == TGA_GREY_RLE )
				texformat.imageEncoding |= TexCache::TexFmt::TEXFMT_RLE;

			return texLoadRaw( f, width, height, 1, 32, 4, TGA_RGBA_MASK, flipV, flipH, hdr[2] == TGA_COLOR_RLE );
		} else if ( depth == 24 ) {
			texformat.imageEncoding |= TexCache::TexFmt::TEXFMT_RGB8;

			if ( hdr[2] == TGA_COLOR_RLE )
				texformat.imageEncoding |= TexCache::TexFmt::TEXFMT_RLE;

			return texLoadRaw( f, width, height, 1, 24, 3, TGA_RGB_MASK, flipV, flipH, hdr[2] == TGA_COLOR_RLE );
		}

		break;
	}

	throw QString( "image sub format not supported" );
	return 0;
}

//! Return value as a 32-bit value; possibly replace with QtEndian functions?
static inline quint32 get32( quint8 * x )
{
	return FileBuffer::readUInt32Fast( x );
}

//! Return value as a 16-bit value; possibly replace with QtEndian functions?
static inline quint16 get16( quint8 * x )
{
	return FileBuffer::readUInt16Fast( x );
}

//! Load a BMP texture.
static GLuint texLoadBMP(
	QIODevice & f, TexCache::TexFmt & texformat, GLenum & target, GLuint & width, GLuint & height, GLuint * id )
{
	// read in bmp header
	quint8 hdr[54];
	qint64 readBytes = f.read( (char *)hdr, 54 );

	if ( readBytes != 54 || strncmp( (char *)hdr, "BM", 2 ) != 0 )
		throw QString( "not a BMP file" );

	texformat.imageFormat = TexCache::TexFmt::TEXFMT_BMP;
	target = GL_TEXTURE_2D;

	glBindTexture( target, id[0] );

	width  = get32( &hdr[18] );
	height = get32( &hdr[22] );
	unsigned int bpp = get16( &hdr[28] );
	unsigned int compression = get32( &hdr[30] );
	unsigned int offset = get32( &hdr[10] );

	f.seek( offset );

	if ( !( isPowerOfTwo( width ) && isPowerOfTwo( height ) ) )
		throw QString( "image dimensions must be power of two" );

	switch ( compression ) {
	case 0:

		if ( bpp == 24 ) {
			return texLoadRaw( f, width, height, 1, bpp, 3, BMP_RGBA_MASK, true );
		}

		break;
	}

	throw QString( "unknown image sub format" );
	/*
	qDebug( "size %i,%i", width, height );
	qDebug( "plns %i", get16( &hdr[26] ) );
	qDebug( "bpp  %i", bpp );
	qDebug( "cmpr %08x", compression );
	qDebug( "ofs  %i", offset );
	*/
	return 0;
}


//! Create texture with glTexStorage2D using GLI
#ifndef Q_OS_MACOS
static GLuint GLI_create_texture(
	NifSkopeOpenGLContext::GLFunctions * fn, gli::texture& texture, GLenum& target, GLuint * id )
{
	gli::gl glProfile( gli::gl::PROFILE_GL33 );
	gli::gl::format const format = glProfile.translate( texture.format(), texture.swizzles() );
	target = glProfile.translate( texture.target() );

	if ( !id[0] )
		glGenTextures( 1, id );
	glBindTexture( target, id[0] );
	glTexParameteri( target, GL_TEXTURE_BASE_LEVEL, 0 );
	glTexParameteri( target, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(texture.levels() - 1) );
	if ( gli::component_count(texture.format()) == 1 ) {
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_R, format.Swizzles[0] );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_G, format.Swizzles[0] );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_B, format.Swizzles[0] );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_A, GL_ONE );
	} else {
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_R, format.Swizzles[0] );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_G, format.Swizzles[1] );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_B, format.Swizzles[2] );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_A, format.Swizzles[3] );
	}

	glm::tvec3<GLsizei> const textureExtent( texture.extent() );

	switch ( texture.target() ) {
	case gli::TARGET_2D:
	case gli::TARGET_CUBE:
		fn->glTexStorage2D( target, static_cast<GLint>(texture.levels()), format.Internal,
							textureExtent.x, textureExtent.y );
		break;
	default:
		return 0;
	}

	for ( size_t layer = 0; layer < texture.layers(); ++layer )
	for ( size_t face = 0; face < texture.faces(); ++face )
	for ( size_t level = 0; level < texture.levels(); ++level ) {
		glm::tvec3<GLsizei> textureLevelExtent( texture.extent( level ) );
		switch ( texture.target() ) {
		case gli::TARGET_2D:
		case gli::TARGET_CUBE:
			if ( gli::is_compressed( texture.format() ) )
				fn->glCompressedTexSubImage2D(
					gli::is_target_cube( texture.target() ) ? static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face)
					: target,
					static_cast<GLint>(level),
					0, 0,
					textureLevelExtent.x, textureLevelExtent.y,
					format.Internal, static_cast<GLsizei>(texture.size( level )),
					texture.data( layer, face, level ) );
			else
				fn->glTexSubImage2D(
					gli::is_target_cube( texture.target() ) ? static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face)
					: target,
					static_cast<GLint>(level),
					0, 0,
					textureLevelExtent.x, textureLevelExtent.y,
					format.External, format.Type,
					texture.data( layer, face, level ) );
			break;
		default:
			return 0;
		}
	}

	return id[0];
}
#else
//! Fallback for systems that do not have glTexStorage2D
static GLuint GLI_create_texture_fallback(
	NifSkopeOpenGLContext::GLFunctions * fn, gli::texture& texture, GLenum & target, GLuint * id )
{
	if ( texture.empty() )
		return 0;

	gli::gl GL( gli::gl::PROFILE_GL33 );
	gli::gl::format const fmt = GL.translate( texture.format(), texture.swizzles() );
	target = GL.translate( texture.target() );

	if ( !id[0] )
		glGenTextures( 1, id );
	glBindTexture( target, id[0] );
	// Base and max level are not supported by OpenGL ES 2.0
	glTexParameteri( target, GL_TEXTURE_BASE_LEVEL, 0 );
	glTexParameteri( target, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(texture.levels() - 1) );
	// Texture swizzle is not supported by OpenGL ES 2.0 and OpenGL 3.2
	if ( gli::component_count(texture.format()) == 1 ) {
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_R, fmt.Swizzles[0] );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_G, fmt.Swizzles[0] );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_B, fmt.Swizzles[0] );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_A, GL_ONE );
	} else {
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_R, fmt.Swizzles[0] );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_G, fmt.Swizzles[1] );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_B, fmt.Swizzles[2] );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_A, fmt.Swizzles[3] );
	}

	for ( std::size_t layer = 0; layer < texture.layers(); ++layer )
	for ( std::size_t face = 0; face < texture.faces(); ++face )
	for ( std::size_t level = 0; level < texture.levels(); ++level ) {
		glm::tvec3<GLsizei> extent( texture.extent( level ) );
		switch ( texture.target() ) {
		case gli::TARGET_2D:
		case gli::TARGET_CUBE:
			if ( gli::is_compressed( texture.format() ) )
				fn->glCompressedTexImage2D(
					gli::is_target_cube( texture.target() ) ? static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face)
					: target,
					static_cast<GLint>(level),
					fmt.Internal,
					extent.x, extent.y,
					0,
					static_cast<GLsizei>(texture.size( level )),
					texture.data( layer, face, level ) );
			else
				fn->glTexImage2D(
					gli::is_target_cube( texture.target() ) ? static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face)
					: target,
					static_cast<GLint>(level),
					fmt.Internal,
					extent.x, extent.y,
					0,
					fmt.External, fmt.Type,
					texture.data( layer, face, level ) );
			break;
		default:
			return 0;
		}
	}
	return id[0];
}
#endif

//! Rewrite of gli::load_dds to not crash on invalid textures
static gli::texture load_if_valid( const char * data, unsigned int size )
{
	using namespace gli;
	using namespace gli::detail;

	if ( std::strncmp( data, FOURCC_DDS, 4 ) != 0 || size < sizeof( dds_header ) )
		return texture();

	std::size_t Offset = sizeof( FOURCC_DDS );

	dds_header const & Header( *reinterpret_cast<dds_header const *>(data + Offset) );
	Offset += sizeof( dds_header );

	dds_header10 Header10;
	if ( (Header.Format.flags & dx::DDPF_FOURCC) && (Header.Format.fourCC == dx::D3DFMT_DX10 || Header.Format.fourCC == dx::D3DFMT_GLI1) ) {
		std::memcpy( &Header10, data + Offset, sizeof( Header10 ) );
		Offset += sizeof( dds_header10 );
	}

	dx DX;

	format Format( static_cast<format>(FORMAT_UNDEFINED) );
	if ( (Header.Format.flags & (dx::DDPF_RGB | dx::DDPF_ALPHAPIXELS | dx::DDPF_ALPHA | dx::DDPF_YUV | dx::DDPF_LUMINANCE | 0x00080000)) && Format == static_cast<format>(gli::FORMAT_UNDEFINED) && Header.Format.bpp != 0 ) {
		switch ( Header.Format.bpp ) {
		default:
			break;
		case 8:
			{
				if ( glm::all( glm::equal( Header.Format.Mask, DX.translate( FORMAT_RG4_UNORM_PACK8 ).Mask ) ) )
					Format = FORMAT_RG4_UNORM_PACK8;
				else if ( glm::all( glm::equal( Header.Format.Mask, DX.translate( FORMAT_L8_UNORM_PACK8 ).Mask ) ) )
					Format = FORMAT_L8_UNORM_PACK8;
				else if ( glm::all( glm::equal( Header.Format.Mask, DX.translate( FORMAT_A8_UNORM_PACK8 ).Mask ) ) )
					Format = FORMAT_A8_UNORM_PACK8;
				else if ( glm::all( glm::equal( Header.Format.Mask, DX.translate( FORMAT_R8_UNORM_PACK8 ).Mask ) ) )
					Format = FORMAT_R8_UNORM_PACK8;
				else if ( glm::all( glm::equal( Header.Format.Mask, DX.translate( FORMAT_RG3B2_UNORM_PACK8 ).Mask ) ) )
					Format = FORMAT_RG3B2_UNORM_PACK8;
				break;
			}
		case 16:
			{
				if ( glm::all( glm::equal( Header.Format.Mask, DX.translate( FORMAT_RGBA4_UNORM_PACK16 ).Mask ) ) )
					Format = FORMAT_RGBA4_UNORM_PACK16;
				else if ( glm::all( glm::equal( Header.Format.Mask, DX.translate( FORMAT_BGRA4_UNORM_PACK16 ).Mask ) ) )
					Format = FORMAT_BGRA4_UNORM_PACK16;
				else if ( glm::all( glm::equal( Header.Format.Mask, DX.translate( FORMAT_R5G6B5_UNORM_PACK16 ).Mask ) ) )
					Format = FORMAT_R5G6B5_UNORM_PACK16;
				else if ( glm::all( glm::equal( Header.Format.Mask, DX.translate( FORMAT_B5G6R5_UNORM_PACK16 ).Mask ) ) )
					Format = FORMAT_B5G6R5_UNORM_PACK16;
				else if ( glm::all( glm::equal( Header.Format.Mask, DX.translate( FORMAT_RGB5A1_UNORM_PACK16 ).Mask ) ) )
					Format = FORMAT_RGB5A1_UNORM_PACK16;
				else if ( glm::all( glm::equal( Header.Format.Mask, DX.translate( FORMAT_BGR5A1_UNORM_PACK16 ).Mask ) ) )
					Format = FORMAT_BGR5A1_UNORM_PACK16;
				else if ( glm::all( glm::equal( Header.Format.Mask, DX.translate( FORMAT_LA8_UNORM_PACK8 ).Mask ) ) )
					Format = FORMAT_LA8_UNORM_PACK8;
				else if ( glm::all( glm::equal( Header.Format.Mask, DX.translate( FORMAT_RG8_UNORM_PACK8 ).Mask ) ) )
					Format = FORMAT_RG8_UNORM_PACK8;
				else if ( glm::all( glm::equal( Header.Format.Mask, DX.translate( FORMAT_L16_UNORM_PACK16 ).Mask ) ) )
					Format = FORMAT_L16_UNORM_PACK16;
				else if ( glm::all( glm::equal( Header.Format.Mask, DX.translate( FORMAT_A16_UNORM_PACK16 ).Mask ) ) )
					Format = FORMAT_A16_UNORM_PACK16;
				else if ( glm::all( glm::equal( Header.Format.Mask, DX.translate( FORMAT_R16_UNORM_PACK16 ).Mask ) ) )
					Format = FORMAT_R16_UNORM_PACK16;
				break;
			}
		case 24:
			{
				if ( glm::all( glm::equal( Header.Format.Mask, DX.translate( FORMAT_RGB8_UNORM_PACK8 ).Mask ) ) )
					Format = FORMAT_RGB8_UNORM_PACK8;
				else if ( glm::all( glm::equal( Header.Format.Mask, DX.translate( FORMAT_BGR8_UNORM_PACK8 ).Mask ) ) )
					Format = FORMAT_BGR8_UNORM_PACK8;
				break;
			}
		case 32:
			{
				if ( glm::all( glm::equal( Header.Format.Mask, DX.translate( FORMAT_BGR8_UNORM_PACK32 ).Mask ) ) )
					Format = FORMAT_BGR8_UNORM_PACK32;
				else if ( glm::all( glm::equal( Header.Format.Mask, DX.translate( FORMAT_BGRA8_UNORM_PACK8 ).Mask ) ) )
					Format = FORMAT_BGRA8_UNORM_PACK8;
				else if ( glm::all( glm::equal( Header.Format.Mask, DX.translate( FORMAT_RGBA8_UNORM_PACK8 ).Mask ) ) )
					Format = ( !( Header.Format.flags & 0x00080000 ) ? FORMAT_RGBA8_UNORM_PACK8 : FORMAT_RGBA8_SNORM_PACK8 );
				else if ( glm::all( glm::equal( Header.Format.Mask, DX.translate( FORMAT_RGB10A2_UNORM_PACK32 ).Mask ) ) )
					Format = FORMAT_RGB10A2_UNORM_PACK32;
				else if ( glm::all( glm::equal( Header.Format.Mask, DX.translate( FORMAT_LA16_UNORM_PACK16 ).Mask ) ) )
					Format = FORMAT_LA16_UNORM_PACK16;
				else if ( glm::all( glm::equal( Header.Format.Mask, DX.translate( FORMAT_RG16_UNORM_PACK16 ).Mask ) ) )
					Format = FORMAT_RG16_UNORM_PACK16;
				else if ( glm::all( glm::equal( Header.Format.Mask, DX.translate( FORMAT_R32_SFLOAT_PACK32 ).Mask ) ) )
					Format = FORMAT_R32_SFLOAT_PACK32;
				break;
			}
		}
	} else if ( (Header.Format.flags & dx::DDPF_FOURCC) && (Header.Format.fourCC != dx::D3DFMT_DX10) && (Header.Format.fourCC != dx::D3DFMT_GLI1) && (Format == static_cast<format>(gli::FORMAT_UNDEFINED)) ) {
		dx::d3dfmt const FourCC = remap_four_cc( Header.Format.fourCC );
		Format = DX.find( FourCC );
	} else if ( Header.Format.fourCC == dx::D3DFMT_DX10 || Header.Format.fourCC == dx::D3DFMT_GLI1 )
		Format = DX.find( Header.Format.fourCC, Header10.Format );

	if ( Format == static_cast<format>(FORMAT_UNDEFINED) )
		return texture();

	size_t const MipMapCount = (Header.Flags & DDSD_MIPMAPCOUNT) ? Header.MipMapLevels : 1;
	size_t FaceCount = 1;
	if ( Header.CubemapFlags & DDSCAPS2_CUBEMAP )
		FaceCount = int( glm::bitCount( Header.CubemapFlags & DDSCAPS2_CUBEMAP_ALLFACES ) );

	size_t DepthCount = 1;
	if ( Header.CubemapFlags & DDSCAPS2_VOLUME )
		DepthCount = Header.Depth;

	texture Texture(
		get_target( Header, Header10 ), Format,
		texture::extent_type( Header.Width, Header.Height, DepthCount ),
		std::max<texture::size_type>( Header10.ArraySize, 1 ), FaceCount, MipMapCount );

	std::size_t const SourceSize = Offset + Texture.size();
	if ( SourceSize > size )
		return texture();

	std::memcpy( Texture.data(), data + Offset, Texture.size() );

	return Texture;
}


GLuint TexCache::texLoadDDS( const QString & filepath, GLenum & target, QByteArray & data, GLuint * id )
{
	if ( data.size() < 128 )
		return 0;

	GLuint mipmaps = 0;
	GLuint result = 0;
	gli::texture texture = load_if_valid( data.constData(), data.size() );
	if ( !texture.empty() && fn ) {
#ifdef Q_OS_MACOS
		result = GLI_create_texture_fallback( fn, texture, target, id );
#else
		result = GLI_create_texture( fn, texture, target, id );
#endif
	}

	if ( result || !fn ) {
		id[0] = result;
		mipmaps = (GLuint)texture.levels();
	} else {
		QString file = filepath;
		Message::append( "One or more textures failed to load.",
							QString( "'%1' is corrupt or unsupported." ).arg( file )
		);
	}

	if ( !texture.empty() )
		texture.clear();

	return mipmaps;
}

static SFCubeMapCache	sfCubeMapCache;

void TexCache::clearCubeCache()
{
	sfCubeMapCache.clear();
}

GLuint TexCache::texLoadPBRCubeMap(
	const NifModel * nif, const QString & filepath, GLenum & target, QByteArray & data, GLuint * id )
{
	if ( data.size() < 148 )
		return 0;

	const unsigned char *	dataPtr = reinterpret_cast< unsigned char * >( data.data() );
	float	normalizeLevel = 1.0f / 12.0f;
	bool	filterDisabled = false;
	do {
		if ( FileBuffer::readUInt64Fast( dataPtr ) == 0x4E41494441523F23ULL ) {	// "#?RADIAN"
			normalizeLevel = float( ( 16 - hdrToneMapLevel ) * ( 16 - hdrToneMapLevel ) + 128 );
			normalizeLevel *= 3.0f / 4096.0f;
			if ( nif->getBSVersion() >= 170 )	// not Fallout 76
				break;
			for ( size_t i = 0; i <= 144; i++ ) {
				std::uint32_t	tmp = FileBuffer::readUInt32Fast( dataPtr + i );
				if ( tmp == 0x20592B0A || tmp == 0x20592D0A ) {	// "\n+Y " or "\n-Y "
					// invert Y axis
					data.data()[i + 1] = char( ((tmp >> 8) & 0xFF) ^ 6 );
					break;
				}
			}
			break;
		}
		if ( FileBuffer::readUInt32Fast( dataPtr ) == 0x20534444 ) {	// "DDS "
			// DDSCAPS2_CUBEMAP
			if ( dataPtr[113] & 0x02 ) {
				// DXGI_FORMAT_R9G9B9E5_SHAREDEXP with mipmaps:
				// assume the texture is already pre-filtered
				if ( FileBuffer::readUInt32Fast( dataPtr + 84 ) == 0x30315844 )	// "DX10"
					filterDisabled = ( dataPtr[128] == 0x43 && dataPtr[28] >= 2 );
				break;
			}
		}
		return 0;
	} while ( false );

	if ( !filterDisabled ) {
		std::uint32_t	width = std::uint32_t( pbrCubeMapResolution );
		sfCubeMapCache.setOutputWidth( width );
		sfCubeMapCache.setRoughnessTable( nullptr, 7 );
		sfCubeMapCache.setNormalizeLevel( normalizeLevel );
		sfCubeMapCache.setImportanceSamplingQuality( pbrImportanceSamples );
		size_t	dataSize = size_t( data.size() );
		size_t	spaceRequired = width * width * 8 * 4 + 148;
		if ( data.size() < qsizetype(spaceRequired) )
			data.resize( spaceRequired );
		size_t	newSize = sfCubeMapCache.convertImage( reinterpret_cast< unsigned char * >(data.data()), dataSize,
														true, spaceRequired, hdrToneMapLevel );
		data.resize( newSize );
	}

	{
		// generate and load second cube map for diffuse lighting
		std::uint32_t	width = 32;
		QByteArray	tmpData( data );
		size_t	dataSize = size_t( tmpData.size() );
		size_t	spaceRequired = width * width * 8 * 4 + 148;
		if ( tmpData.size() < qsizetype(spaceRequired) )
			tmpData.resize( spaceRequired );
		static const float  roughnessDiffuse = 1.0f;
		sfCubeMapCache.setOutputWidth( width );
		sfCubeMapCache.setRoughnessTable( &roughnessDiffuse, 1 );
		sfCubeMapCache.setImportanceSamplingQuality( -1 );
		size_t	newSize = sfCubeMapCache.convertImage( reinterpret_cast< unsigned char * >(tmpData.data()), dataSize,
														true, spaceRequired );
		tmpData.resize( newSize );
		(void) texLoadDDS( filepath, target, tmpData, id + 1 );
	}

	return texLoadDDS( filepath, target, data, id );
}

GLuint TexCache::texLoadColor( const NifModel * nif, const QString & filepath,
								GLenum & target, GLuint & width, GLuint & height, QByteArray & data, GLuint * id )
{
	// generate 1x1 texture from an RGBA color in "#AABBGGRR" format
	QChar	c;
	if ( filepath.length() >= 10 )
		c = filepath.back().toLower();
	bool	isCubeMap = ( c == 'c' );
	std::uint32_t	color = 0;
	for ( int i = 1; i < 9; i++ ) {
		color = color << 4;
		unsigned short	tmp = filepath.at( i ).toLower().unicode();
		color = color | ( ( tmp + ( (tmp >> 6) * 9 ) ) & 0x0F );
	}
	width = 1;
	height = 1;
	int	n = ( !isCubeMap ? 1 : 6 );
	data.resize( n * 4 + 148 );
	unsigned char	dxgiFmt = 0x1C;		// DXGI_FORMAT_R8G8B8A8_UNORM
	if ( c == 's' || c == 'c' ) {
		dxgiFmt = 0x1D;					// DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
	} else if ( c == 'n' ) {
		dxgiFmt = 0x1F;					// DXGI_FORMAT_R8G8B8A8_SNORM
		color = color ^ 0x80808080U;
	}
	unsigned char *	dataPtr = reinterpret_cast< unsigned char * >( data.data() );
	(void) FileBuffer::writeDDSHeader( dataPtr, dxgiFmt, 1, 1, 1, isCubeMap );
	for ( int i = 0; i < n; i++ )
		FileBuffer::writeUInt32Fast( dataPtr + ( 148 + (i << 2) ), color );

	if ( isCubeMap && nif && nif->getBSVersion() >= 151 )
		return texLoadPBRCubeMap( nif, filepath, target, data, id );
	return texLoadDDS( filepath, target, data, id );
}

// (public function, documented in gltexloaders.h)
GLuint TexCache::texLoad( const QModelIndex & iData, TexFmt & texformat,
							GLenum & target, GLuint & width, GLuint & height, GLuint * id )
{
	GLuint mipmaps = 0;

	auto nif = NifModel::fromValidIndex(iData);
	if ( nif ) {
		mipmaps = nif->get<uint>( iData, "Num Mipmaps" );
		QModelIndex iMipmaps = nif->getIndex( iData, "Mipmaps" );

		if ( mipmaps > 0 && iMipmaps.isValid() ) {
			QModelIndex iMipmap = nif->getIndex( iMipmaps, 0 );
			width  = nif->get<uint>( iMipmap, "Width" );
			height = nif->get<uint>( iMipmap, "Height" );
		}

		int bpp = nif->get<uint>( iData, "Bits Per Pixel" );
		int bytespp = nif->get<uint>( iData, "Bytes Per Pixel" );
		uint format = nif->get<uint>( iData, "Pixel Format" );
		bool flipV  = false;
		bool flipH  = false;
		bool rle = false;

		QBuffer buf;

		QModelIndex iPixelData = nif->getIndex( iData, "Pixel Data" );

		if ( iPixelData.isValid() ) {
			if ( QByteArray * pdata = nif->get<QByteArray *>( nif->getIndex( iPixelData, 0 ) ) ) {
				buf.setData( *pdata );
				buf.open( QIODevice::ReadOnly );
				buf.seek( 0 );
			}
		}

		if ( buf.size() == 0 )
			return false;

		quint32 mask[4] = {
			0x00000000, 0x00000000, 0x00000000, 0x00000000
		};

		if ( nif->getVersionNumber() < 0x14000004 ) {
			mask[0] = nif->get<uint>( iData, "Red Mask" );
			mask[1] = nif->get<uint>( iData, "Green Mask" );
			mask[2] = nif->get<uint>( iData, "Blue Mask" );
			mask[3] = nif->get<uint>( iData, "Alpha Mask" );
		} else {
			QModelIndex iChannels = nif->getIndex( iData, "Channels" );

			if ( iChannels.isValid() ) {
				for ( int i = 0; i < 4; i++ ) {
					QModelIndex iChannel = nif->getIndex( iChannels, i );
					uint type = nif->get<uint>( iChannel, "Type" );
					uint bpc  = nif->get<uint>( iChannel, "Bits Per Channel" );
					int m = (1 << bpc) - 1;

					switch ( type ) {
					case 0:
						mask[i] = m << (bpc * 0);
						break;     // Green
					case 1:
						mask[i] = m << (bpc * 1);
						break;     // Blue
					case 2:
						mask[i] = m << (bpc * 2);
						break;     // Red
					case 3:
						mask[i] = m << (bpc * 3);
						break;     // Red
					}
				}
			}
		}

		DDS_HEADER hdr = {};
		hdr.dwSize = sizeof( hdr );
		hdr.dwHeaderFlags = DDS_HEADER_FLAGS_TEXTURE | DDS_HEADER_FLAGS_LINEARSIZE | DDS_HEADER_FLAGS_MIPMAP;
		hdr.dwHeight = height;
		hdr.dwWidth = width;
		hdr.dwMipMapCount = mipmaps;
		hdr.ddspf.dwFlags = DDS_FOURCC;
		hdr.ddspf.dwSize = sizeof( DDS_PIXELFORMAT );
		hdr.ddspf.dwRBitMask = mask[0];
		hdr.ddspf.dwGBitMask = mask[1];
		hdr.ddspf.dwBBitMask = mask[2];
		hdr.ddspf.dwRBitMask = mask[3];
		hdr.dwSurfaceFlags = DDS_SURFACE_FLAGS_TEXTURE | DDS_SURFACE_FLAGS_MIPMAP;

		texformat.imageFormat = TexFmt::TEXFMT_NIF;

		switch ( format ) {
		case 0: // PX_FMT_RGB8
			texformat.imageEncoding |= TexFmt::TEXFMT_RGB8;
			mipmaps = texLoadRaw( buf, width, height, mipmaps, bpp, bytespp, mask, flipV, flipH, rle );
			break;
		case 1: // PX_FMT_RGBA8
			texformat.imageEncoding |= TexFmt::TEXFMT_RGBA8;
			mipmaps = texLoadRaw( buf, width, height, mipmaps, bpp, bytespp, mask, flipV, flipH, rle );
			break;
		case 2: // PX_FMT_PAL8
			{
				texformat.imageEncoding |= TexFmt::TEXFMT_PAL8;
				// Read the NiPalette entries; change this if we change NiPalette in nif.xml
				QModelIndex iPalette = nif->getBlockIndex( nif->getLink( iData, "Palette" ) );

				if ( iPalette.isValid() ) {
					QVector<quint32> map;
					uint nmap = nif->get<uint>( iPalette, "Num Entries" );
					map.resize( nmap );
					QModelIndex iPaletteArray = nif->getIndex( iPalette, "Palette" );

					if ( nmap > 0 && iPaletteArray.isValid() ) {
						for ( uint i = 0; i < nmap; ++i ) {
							auto color = nif->get<ByteColor4>( nif->getIndex( iPaletteArray, i ) ).toQColor();
							quint8 r = color.red();
							quint8 g = color.green();
							quint8 b = color.blue();
							quint8 a = color.alpha();
							map[i] = ( (quint32)( ( r | ( (quint16)g << 8 ) ) | ( ( (quint32)b ) << 16 ) | ( ( (quint32)a ) << 24 ) ) );
						}
					}

					mipmaps = texLoadPal( buf, width, height, mipmaps, bpp, bytespp, map.data(), flipV, flipH, rle );
				}
			}
			break;
		case 4: //PX_FMT_DXT1
			texformat.imageEncoding |= TexFmt::TEXFMT_DXT1;
			hdr.ddspf.dwFourCC = FOURCC_DXT1;
			hdr.dwPitchOrLinearSize = width * height / 2;
			break;
		case 5: //PX_FMT_DXT3
			texformat.imageEncoding |= TexFmt::TEXFMT_DXT3;
			hdr.ddspf.dwFourCC = FOURCC_DXT3;
			hdr.dwPitchOrLinearSize = width * height;
			break;
		case 6: //PX_FMT_DXT5
			texformat.imageEncoding |= TexFmt::TEXFMT_DXT5;
			hdr.ddspf.dwFourCC = FOURCC_DXT5;
			hdr.dwPitchOrLinearSize = width * height;
			break;
		}

		if ( format >= 4 && format <= 6 ) {
			// Create and prepend DDS header
			char dds[sizeof(hdr)];
			memcpy( dds, &hdr, sizeof(hdr) );

			buf.buffer().prepend( QByteArray::fromRawData( dds, sizeof( hdr ) ) );
			buf.buffer().prepend( QByteArray::fromStdString( "DDS " ) );

			mipmaps = texLoadDDS( QString( "[%1] NiPixelData" ).arg( nif->getBlockNumber( iData ) ),
									target, buf.buffer(), id );
		}
	}

	return mipmaps;
}

//! Load NiPixelData or NiPersistentSrcTextureRendererData from a NifModel
GLuint TexCache::texLoadNIF( QIODevice & f, TexFmt & texformat,
								GLenum & target, GLuint & width, GLuint & height, GLuint * id )
{
	GLuint mipmaps = 0;
	target = GL_TEXTURE_2D;

	glBindTexture( target, id[0] );

	NifModel pix;

	if ( !pix.load( f ) )
		throw QString( "failed to load pixel data from file" );

	QPersistentModelIndex iRoot;

	for ( const auto l : pix.getRootLinks() ) {
		QModelIndex iData = pix.getBlockIndex( l, "NiPixelFormat" );

		if ( !iData.isValid() || iData == QModelIndex() )
			throw QString( "this is not a normal .nif file; there should be only pixel data as root blocks" );

		mipmaps = texLoad( iData, texformat, target, width, height, id );
	}

	return mipmaps;
}


static void extract_pbr_lut_data( QByteArray & data )
{
	static QByteArray	pbrLUTData;
	if ( pbrLUTData.isEmpty() ) {
		SF_PBR_Tables	pbrLUT( 512, 4096 );
		pbrLUTData.resize( qsizetype( pbrLUT.getImageData().size() ) );
		std::memcpy( pbrLUTData.data(), pbrLUT.getImageData().data(), pbrLUT.getImageData().size() );
	}
	data = pbrLUTData;
}

GLuint TexCache::texLoad( const NifModel * nif, const QString & filepath,
							TexFmt & format, GLenum & target, GLuint & width, GLuint & height, GLuint * id )
{
	width = height = 0;
	GLuint	mipmaps = 0;

	QByteArray	data;
	if ( filepath.startsWith(QChar('#')) && (filepath.length() == 9 || filepath.length() == 10) ) {
		if ( filepath == "#sfpbr.dds" )
			extract_pbr_lut_data( data );
		else
			return texLoadColor( nif, filepath, target, width, height, data, id );
	} else {
		bool	fileFound;
		if ( !nif )
			fileFound = Game::GameManager::get_file( data, Game::OTHER, filepath, "textures", "" );
		else
			fileFound = nif->getResourceFile( data, filepath, "textures", "" );
		if ( !fileFound )
			throw QString( "could not open file" );
	}

	if ( data.isEmpty() )
		return 0;

	if ( filepath.endsWith( QLatin1StringView(".dds"), Qt::CaseInsensitive )
		|| ( filepath.endsWith( QLatin1StringView(".hdr"), Qt::CaseInsensitive )
			&& nif && nif->getBSVersion() >= 151 ) ) {
		bool	isCubeMap = false;
		if ( data.size() >= 148 ) {
			if ( FileBuffer::readUInt32Fast( data.data() ) == 0x20534444 ) {	// "DDS "
				if ( data.data()[113] & 0x02 ) {	// DDSCAPS2_CUBEMAP
					isCubeMap = true;
					if ( nif->getBSVersion() < 170 && FileBuffer::readUInt32Fast( data.data() + 84 ) == 0x30315844 && data.data()[128] == 0x57 )
						data[128] = 0x5B;	// Fallout 76: DXGI_FORMAT_B8G8R8A8_UNORM -> DXGI_FORMAT_B8G8R8A8_UNORM_SRGB
				}
			} else if ( FileBuffer::readUInt64Fast( data.data() ) == 0x4E41494441523F23ULL ) {	// "#?RADIAN"
				isCubeMap = true;
			}
		}
		if ( isCubeMap && nif && nif->getBSVersion() >= 151 ) {
			mipmaps = texLoadPBRCubeMap( nif, filepath, target, data, id );
		} else {
			mipmaps = texLoadDDS( filepath, target, data, id );
		}
	} else {
		QBuffer f( &data );
		if ( !f.open( QIODevice::ReadWrite ) )
			throw QString( "could not open buffer" );

		if ( filepath.endsWith( QLatin1StringView(".tga"), Qt::CaseInsensitive ) )
			mipmaps = texLoadTGA( f, format, target, width, height, id );
		else if ( filepath.endsWith( QLatin1StringView(".bmp"), Qt::CaseInsensitive ) )
			mipmaps = texLoadBMP( f, format, target, width, height, id );
		else if ( filepath.endsWith( QLatin1StringView(".nif"), Qt::CaseInsensitive )
					|| filepath.endsWith( QLatin1StringView(".texcache"), Qt::CaseInsensitive ) )
			mipmaps = texLoadNIF( f, format, target, width, height, id );

		f.close();
	}
	data.clear();

	if ( !target )
		target = GL_TEXTURE_2D;

	if ( mipmaps > 0 ) {
		GLenum t = target;
		if ( target == GL_TEXTURE_CUBE_MAP )
			t = GL_TEXTURE_CUBE_MAP_POSITIVE_X;

		glGetTexLevelParameteriv( t, 0, GL_TEXTURE_WIDTH, (GLint *)&width );
		glGetTexLevelParameteriv( t, 0, GL_TEXTURE_HEIGHT, (GLint *)&height );
		glGetTexLevelParameteriv( t, 0, GL_TEXTURE_INTERNAL_FORMAT, (GLint *)&format.internalFormat );
		switch ( format.internalFormat ) {
		case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
		case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
			format.imageEncoding = TexFmt::TEXFMT_DXT1;
			break;
		case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
			format.imageEncoding = TexFmt::TEXFMT_DXT3;
			break;
		case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
			format.imageEncoding = TexFmt::TEXFMT_DXT5;
			break;
		}
		GLint tmp = 0;
		glGetTexLevelParameteriv( t, 0, GL_TEXTURE_COMPRESSED, &tmp );
		format.isCompressed = bool( tmp );
	} else {
		throw QString( "unknown texture format" );
	}

	// Power of Two check
	if ( (width & (width - 1)) || (height & (height - 1)) ) {
		QString file = filepath;
		Message::append( "One or more texture dimensions are not a power of two.",
							QString( "'%1' is %2 x %3." ).arg( file ).arg( width ).arg( height )
		);
	}

	return mipmaps;
}

bool TexCache::texIsSupported( const QString & filepath )
{
	return ( filepath.endsWith( QLatin1StringView(".dds"), Qt::CaseInsensitive )
					|| ( filepath.startsWith( QChar('#') ) && ( filepath.length() == 9 || filepath.length() == 10 ) )
					|| filepath.endsWith( QLatin1StringView(".tga"), Qt::CaseInsensitive )
					|| filepath.endsWith( QLatin1StringView(".bmp"), Qt::CaseInsensitive )
					|| filepath.endsWith( QLatin1StringView(".nif"), Qt::CaseInsensitive )
					|| filepath.endsWith( QLatin1StringView(".texcache"), Qt::CaseInsensitive )
					|| filepath.endsWith( QLatin1StringView(".hdr"), Qt::CaseInsensitive )
	);
}

bool TexCache::texCanLoad( const QString & filepath )
{
	QFileInfo i( filepath );
	return i.exists() && i.isReadable() && texIsSupported( filepath );
}


bool TexCache::texSaveDDS( const QModelIndex & index, const QString & filepath,
							const GLuint & width, const GLuint & height, const GLuint & mipmaps )
{
	auto nif = NifModel::fromIndex( index );
	quint32 format = nif->get<quint32>( index, "Pixel Format" );

	// can't dump palettised textures yet
	if ( format == 2 ) {
		qCCritical( nsIo ) << QObject::tr( "Texture format not supported" );
		return false;
	}

	// copy directly from mipmaps into texture

	QBuffer buf;

	QModelIndex iPixelData = nif->getIndex( index, "Pixel Data" );

	if ( iPixelData.isValid() ) {
		if ( QByteArray * pdata = nif->get<QByteArray *>( nif->getIndex( iPixelData, 0 ) ) ) {
			buf.setData( *pdata );
			buf.open( QIODevice::ReadOnly );
			buf.seek( 0 );
		}
	}

	if ( buf.size() == 0 )
		return false;

	QString filename = filepath;

	if ( !filename.toLower().endsWith( ".dds" ) )
		filename.append( ".dds" );

	QFile f( filename );

	if ( !f.open( QIODevice::WriteOnly ) ) {
		qCCritical( nsIo ) << QObject::tr( "texSaveDDS: could not open %1" ).arg( filename );
		return false;
	}

	qint64 writeBytes = f.write( (char *)"DDS ", 4 );

	if ( writeBytes != 4 ) {
		qCCritical( nsIo ) << QObject::tr( "texSaveDDS: could not open %1" ).arg( filename );
		return false;
	}

	quint8 header[124]; // could probably use a bytearray or something here

	for ( int o = 0; o < 124; o++ )
		header[o] = 0;

	int pos = 0; // offset

	// size of header
	header[pos] = 124;
	pos += 4;

	bool compressed = ( (format == 4) || (format == 5) || (format == 6) );

	// header flags
	header[pos++] = ( 1 << 0 )                       // caps = 1
									| ( 1 << 1 )                     // height = 1
									| ( 1 << 2 )                     // width = 1
									| ( (compressed ? 0 : 1) << 3 ); // pitch, and 4 bits reserved

	header[pos++] = ( 1 << 4 );                      // 4 bits reserved, pixelformat = 1, 3 bits reserved

	//quint32 mipmaps = nif->get<quint32>( index, "Num Mipmaps" );

	bool hasMipMaps = ( mipmaps > 1 );

	header[pos++] = ( (hasMipMaps ? 1 : 0) << 1 )    // 1 bit reserved, mipmapcount
									| ( (compressed ? 1 : 0) << 3 ); // 1 bit reserved, linearsize, 3 bits reserved, depth = 0

	pos++;

	// height
	qToLittleEndian( height, header + pos );
	pos += 4;

	// width
	qToLittleEndian( width, header + pos );
	pos += 4;

	// linear size
	// Uncompressed textures: bytes per scan line;
	// DXT1: 16-pixel blocks converted to 64 bits = 8 bytes, ie. width * height / 16 * 64 / 8
	// DXT5: don't know
	quint32 linearsize;

	switch ( format ) {
	case 0:
		linearsize = width * 3;
		break;
	case 1:
		linearsize = width * 4;
		break;
	case 4:
		linearsize = width * height / 2;
		break;
	case 5:
	case 6:
		linearsize = width * height;
		break;
	default:     // how did we get here?
		linearsize = 0;
		break;
	}

	qToLittleEndian( linearsize, header + pos );
	pos += 4;

	// depth
	pos += 4;

	// mipmapcount
	qToLittleEndian( mipmaps, header + pos );
	pos += 4;

	// reserved[11]
	pos += 44;

	// pixel format size
	header[pos] = 32;
	pos += 4;

	// pixel format flags
	bool alphapixels = (format == 1 );             //|| format == 5 || format == 6);
	header[pos] = ( (alphapixels ? 1 : 0) << 0 )   // alpha pixels
								| ( (alphapixels ? 1 : 0) << 1 ) // alpha
								| ( (compressed ? 1 : 0) << 2 )  // fourcc
								| ( (compressed ? 0 : 1) << 6 ); // rgb
	pos += 4;

	// pixel format fourcc
	quint32 fourcc;

	switch ( format ) {
	case 0:
	case 1:
		fourcc = 0;
		break;
	case 4:
		fourcc = FOURCC_DXT1;
		break;
	case 5:
		fourcc = FOURCC_DXT3;
		break;
	case 6:
		fourcc = FOURCC_DXT5;
		break;
	default:
		fourcc = 0;
		break;
	}

	qToLittleEndian( fourcc, header + pos );
	pos += 4;

	// bitcount
	// NIF might be wrong, so hardcode based on format
	// quint32 bitcount = nif->get<quint8>( index, "Bits Per Pixel" );
	quint32 bitcount;

	switch ( format ) {
	case 0:
	case 4:
		bitcount = 24;
		break;
	case 1:
	case 5:
	case 6:
		bitcount = 32;
		break;
	default:     // what?
		bitcount = 32;
		break;
	}

	qToLittleEndian( bitcount, header + pos );
	pos += 4;

	// masks
	quint32 mask[4] = {
		0x000000ff, 0x0000ff00, 0x00ff0000, 0x00000000
	};

	if ( nif->getVersionNumber() < 0x14000004 ) {
		mask[0] = nif->get<quint32>( index, "Red Mask" );
		mask[1] = nif->get<quint32>( index, "Green Mask" );
		mask[2] = nif->get<quint32>( index, "Blue Mask" );
		mask[3] = nif->get<quint32>( index, "Alpha Mask" );
	} else {
		QModelIndex iChannels = nif->getIndex( index, "Channels" );

		if ( iChannels.isValid() ) {
			for ( int i = 0; i < 4; i++ ) {
				QModelIndex iChannel = nif->getIndex( iChannels, i );
				uint type = nif->get<uint>( iChannel, "Type" );
				uint bpc  = nif->get<uint>( iChannel, "Bits Per Channel" );
				int m = (1 << bpc) - 1;

				switch ( type ) {
				case 0:
					mask[i] = m << (bpc * 0);
					break;         // Green
				case 1:
					mask[i] = m << (bpc * 1);
					break;         // Blue
				case 2:
					mask[i] = m << (bpc * 2);
					break;         // Red
				case 3:
					mask[i] = m << (bpc * 3);
					break;         // Red
				}
			}
		}
	}

	/*
	if ( alphapixels )
	{
		mask[3] = 0xff000000;
	}*/

	// red mask
	qToLittleEndian( mask[0], header + pos );
	pos += 4;
	// green mask
	qToLittleEndian( mask[1], header + pos );
	pos += 4;
	// blue mask
	qToLittleEndian( mask[2], header + pos );
	pos += 4;
	// alpha mask
	qToLittleEndian( mask[3], header + pos );
	pos += 4;

	// caps1
	header[pos++] = ( (hasMipMaps ? 1 : 0) << 3 ); // complex
	header[pos++] = ( 1 << 4 );                    // texture
	header[pos++] = ( (hasMipMaps ? 1 : 0) << 6 ); // mipmaps

	// write header
	writeBytes = f.write( (char *)header, 124 );

	if ( writeBytes != 124 ) {
		qCCritical( nsIo ) << QObject::tr( "texSaveDDS: could not open %1" ).arg( filename );
		return false;
	}

	// write pixel data
	writeBytes = f.write( buf.data().constData(), buf.size() );

	if ( writeBytes != buf.size() ) {
		qCCritical( nsIo ) << QObject::tr( "texSaveDDS: could not open %1" ).arg( filename );
		return false;
	}

	return true;
}


bool TexCache::texSaveTGA( const QModelIndex & index, const QString & filepath,
							const GLuint & width, const GLuint & height )
{
	Q_UNUSED( index );
	// auto nif = NifModel::fromIndex(index);
	QString filename = filepath;

	if ( !filename.toLower().endsWith( ".tga" ) )
		filename.append( ".tga" );

	//quint32 bytespp = nif->get<quint32>( index, "Bytes Per Pixel" );
	//quint32 bpp = nif->get<quint8>( index, "Bits Per Pixel" );

	quint32 s = width * height * 4; //bytespp;

	quint8 * pixl = (quint8 *)malloc( s );
	quint8 * data = (quint8 *)malloc( s );

	glPixelStorei( GL_PACK_ALIGNMENT, 1 );
	glPixelStorei( GL_PACK_SWAP_BYTES, GL_FALSE );

	// This is very dodgy and is mainly to avoid having to run through convertToRGBA
	// It also won't work with DXT compressed or palletised textures
	/*
	if( nif->get<quint32>( index, "Pixel Format" ) == 0 ) {
		glGetTexImage( GL_TEXTURE_2D, 0, GL_BGR, GL_UNSIGNED_BYTE, data );
	}
	else if( nif->get<quint32>( index, "Pixel Format") == 1 ) {
		glGetTexImage( GL_TEXTURE_2D, 0, GL_BGRA, GL_UNSIGNED_BYTE, data );
	}*/

	glGetTexImage( GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixl );

	//quint32 mask[4] = { 0x00000000, 0x00000000, 0x00000000, 0x00000000 };
	/*
	if ( nif->getVersionNumber() < 0x14000004 ) {
		mask[0] = nif->get<uint>(index, "Blue Mask");
		mask[1] = nif->get<uint>(index, "Green Mask");
		mask[2] = nif->get<uint>(index, "Red Mask");
		mask[3] = nif->get<uint>(index, "Alpha Mask");
	}
	*/

	convertToRGBA( pixl, width, height, 4, TGA_RGBA_MASK, true, false, data );

	free( pixl );

	QFile f( filename );

	if ( !f.open( QIODevice::WriteOnly ) ) {
		qCCritical( nsIo ) << QObject::tr( "texSaveTGA: could not open %1" ).arg( filename );
		free( data );
		return false;
	}

	// write out tga header
	quint8 hdr[18];

	for ( int o = 0; o < 18; o++ )
		hdr[o] = 0;

	hdr[02] = TGA_COLOR;
	hdr[12] = width % 256;
	hdr[13] = width / 256;
	hdr[14] = height % 256;
	hdr[15] = height / 256;
	hdr[16] = 32; //bpp;
	hdr[17] = 8;  //(bytespp == 4 ? 8 : 0)+32;

	quint8 footer[26];

	// extension area
	for ( int o = 0; o < 4; o++ )
		footer[o] = 0;

	// developer area
	for ( int o = 4; o < 8; o++ )
		footer[o] = 0;

	// signature
	const char * signature = "TRUEVISION-XFILE.";
	memcpy( footer + 8, signature, 17 );
	footer[25] = 0;

	qint64 writeBytes = f.write( (char *)hdr, 18 );

	if ( writeBytes != 18 ) {
		qCCritical( nsIo ) << QObject::tr( "texSaveTGA: could not write to %1" ).arg( filename );
		free( data );
		return false;
	}

	writeBytes = f.write( (char *)data, s );

	if ( writeBytes != s ) {
		qCCritical( nsIo ) << QObject::tr( "texSaveTGA: could not write to %1" ).arg( filename );
		free( data );
		return false;
	}

	writeBytes = f.write( (char *)footer, 26 );

	if ( writeBytes != 26 ) {
		qCCritical( nsIo ) << QObject::tr( "texSaveTGA: could not write to %1" ).arg( filename );
		free( data );
		return false;
	}

	free( data );
	return true;
}


bool TexCache::texSaveNIF( NifModel * nif, const QString & filepath, QModelIndex & iData )
{
	// Work out the extension and format
	// If DDS raw, DXT1 or DXT5, copy directly from texture
	//qDebug() << "texSaveNIF: saving" << filepath << "to" << iData;

	QByteArray	fileData;
	if ( !nif->getResourceFile( fileData, filepath, "textures/", nullptr ) )
		throw QString( "could not open file" );

	QBuffer	f( &fileData );
	f.open( QIODevice::ReadOnly );

	if ( filepath.endsWith( ".nif", Qt::CaseInsensitive ) || filepath.endsWith( ".texcache", Qt::CaseInsensitive ) ) {
		// NIF-to-NIF copy
		NifModel pix;

		if ( !pix.load( f ) )
			throw QString( "failed to load NiPixelData from file" );

		// possibly update this to ATextureRenderData...
		QPersistentModelIndex iPixData;
		iPixData = pix.getBlockIndex( 0, "NiPixelData" );

		if ( !iPixData.isValid() )
			throw QString( "Texture .nifs should only have NiPixelData blocks" );

		nif->set<int>( iData, "Pixel Format", pix.get<int>( iPixData, "Pixel Format" ) );

		if ( nif->checkVersion( 0, 0x0A020000 ) && pix.checkVersion( 0, 0x0A020000 ) ) {
			// copy masks
			nif->set<quint32>( iData, "Red Mask", pix.get<quint32>( iPixData, "Red Mask" ) );
			nif->set<quint32>( iData, "Green Mask", pix.get<quint32>( iPixData, "Green Mask" ) );
			nif->set<quint32>( iData, "Blue Mask", pix.get<quint32>( iPixData, "Blue Mask" ) );
			nif->set<quint32>( iData, "Alpha Mask", pix.get<quint32>( iPixData, "Alpha Mask" ) );
			nif->set<quint32>( iData, "Bits Per Pixel", pix.get<quint32>( iPixData, "Bits Per Pixel" ) );

			QModelIndex fastCompareSrc = pix.getIndex( iPixData, "Old Fast Compare" );
			QModelIndex fastCompareDest = nif->getIndex( iData, "Old Fast Compare" );

			for ( int i = 0; i < pix.rowCount( fastCompareSrc ); i++ ) {
				nif->set<quint8>( nif->getIndex( fastCompareDest, i ), pix.get<quint8>( nif->getIndex( fastCompareSrc, i ) ) );
			}

			if ( nif->checkVersion( 0x0A010000, 0x0A020000 ) && pix.checkVersion( 0x0A010000, 0x0A020000 ) ) {
				nif->set<quint32>( iData, "Tiling", pix.get<quint32>( iPixData, "Tiling" ) );
			}
		} else if ( nif->checkVersion( 0x14000004, 0 ) && pix.checkVersion( 0x14000004, 0 ) ) {
			nif->set<quint32>( iData, "Bits Per Pixel", pix.get<quint32>( iPixData, "Bits Per Pixel" ) );
			nif->set<int>( iData, "Renderer Hint", pix.get<int>( iPixData, "Renderer Hint" ) );
			nif->set<quint32>( iData, "Extra Data", pix.get<quint32>( iPixData, "Extra Data" ) );
			nif->set<quint8>( iData, "Flags", pix.get<quint8>( iPixData, "Flags" ) );
			nif->set<quint32>( iData, "Tiling", pix.get<quint32>( iPixData, "Tiling" ) );

			if ( nif->checkVersion( 0x14030006, 0 ) && pix.checkVersion( 0x14030006, 0 ) ) {
				nif->set<quint8>( iData, "sRGB Space", pix.get<quint8>( iPixData, "sRGB Space" ) );
			}

			QModelIndex srcChannels  = pix.getIndex( iPixData, "Channels" );
			QModelIndex destChannels = nif->getIndex( iData, "Channels" );

			// copy channels
			for ( int i = 0; i < 4; i++ ) {
				qDebug() << "Channel" << i;
				qDebug() << pix.get<quint32>( nif->getIndex( srcChannels, i ), "Type" );
				qDebug() << pix.get<quint32>( nif->getIndex( srcChannels, i ), "Convention" );
				qDebug() << pix.get<quint8>( nif->getIndex( srcChannels, i ), "Bits Per Channel" );
				qDebug() << pix.get<quint8>( nif->getIndex( srcChannels, i ), "Is Signed" );

				nif->set<quint32>( nif->getIndex( destChannels, i ), "Type", pix.get<quint32>( nif->getIndex( srcChannels, i ), "Type" ) );
				nif->set<quint32>( nif->getIndex( destChannels, i ), "Convention", pix.get<quint32>( nif->getIndex( srcChannels, i ), "Convention" ) );
				nif->set<quint8>( nif->getIndex( destChannels, i ), "Bits Per Channel", pix.get<quint8>( nif->getIndex( srcChannels, i ), "Bits Per Channel" ) );
				nif->set<quint8>( nif->getIndex( destChannels, i ), "Is Signed", pix.get<quint8>( nif->getIndex( srcChannels, i ), "Is Signed" ) );
			}

			nif->set<quint32>( iData, "Num Faces", pix.get<quint32>( iPixData, "Num Faces" ) );
		} else {
			qCCritical( nsNif ) << QObject::tr( "NIF versions are incompatible, cannot copy pixel data" );
			return false;
		}

		// ignore palette for now; what uses these? theoretically they don't exist past 4.2.2.0

/*
		<add name="Palette" type="Ref" template="NiPalette">Link to NiPalette, for 8-bit textures.</add>
		</niobject>
*/

		nif->set<quint32>( iData, "Num Mipmaps", pix.get<quint32>( iPixData, "Num Mipmaps" ) );
		nif->set<quint32>( iData, "Bytes Per Pixel", pix.get<quint32>( iPixData, "Bytes Per Pixel" ) );

		QModelIndex srcMipMaps  = pix.getIndex( iPixData, "Mipmaps" );
		QModelIndex destMipMaps = nif->getIndex( iData, "Mipmaps" );
		nif->updateArraySize( destMipMaps );

		for ( int i = 0; i < pix.rowCount( srcMipMaps ); i++ ) {
			nif->set<quint32>( nif->getIndex( destMipMaps, i ), "Width", pix.get<quint32>( nif->getIndex( srcMipMaps, i ), "Width" ) );
			nif->set<quint32>( nif->getIndex( destMipMaps, i ), "Height", pix.get<quint32>( nif->getIndex( srcMipMaps, i ), "Height" ) );
			nif->set<quint32>( nif->getIndex( destMipMaps, i ), "Offset", pix.get<quint32>( nif->getIndex( srcMipMaps, i ), "Offset" ) );
		}

		nif->set<quint32>( iData, "Num Pixels", pix.get<quint32>( iPixData, "Num Pixels" ) );

		QModelIndex srcPixelData  = pix.getIndex( iPixData, "Pixel Data" );
		QModelIndex destPixelData = nif->getIndex( iData, "Pixel Data" );

		for ( int i = 0; i < pix.rowCount( srcPixelData ); i++ ) {
			nif->updateArraySize( nif->getIndex( destPixelData, i ) );
			nif->set<QByteArray>( nif->getIndex( destPixelData, i ), "Pixel Data", pix.get<QByteArray>( nif->getIndex( srcPixelData, i ), "Pixel Data" ) );
		}

		//nif->set<>( iData, "", pix.get<>( iPixData, "" ) );
		//nif->set<>( iData, "", pix.get<>( iPixData, "" ) );
	} else if ( filepath.endsWith( ".bmp", Qt::CaseInsensitive ) || filepath.endsWith( ".tga", Qt::CaseInsensitive ) ) {
		//qDebug() << "Copying from GL buffer";

		GLuint width, height, mipmaps;
		GLuint target = GL_TEXTURE_2D;
		TexFmt format;
		GLuint id[2];
		id[0] = 0;
		id[1] = 0;

		// fastest way to get parameters and ensure texture is active
		if ( ( mipmaps = texLoad( nif, filepath, format, target, width, height, id ) ) > 0 ) {
			//qDebug() << "Width" << width << "height" << height << "mipmaps" << mipmaps << "format" << format.toString();
		} else {
			qCCritical( nsIo ) << QObject::tr( "Error importing %1" ).arg( filepath );
			return false;
		}

		// set texture as RGBA
		nif->set<quint32>( iData, "Pixel Format", 1 );
		nif->set<quint32>( iData, "Bits Per Pixel", 32 );

		if ( nif->checkVersion( 0, 0x0A020000 ) ) {
			// set masks
			nif->set<quint32>( iData, "Red Mask", RGBA_INV_MASK[0] );
			nif->set<quint32>( iData, "Green Mask", RGBA_INV_MASK[1] );
			nif->set<quint32>( iData, "Blue Mask", RGBA_INV_MASK[2] );
			nif->set<quint32>( iData, "Alpha Mask", RGBA_INV_MASK[3] );

			QModelIndex oldFastCompare = nif->getIndex( iData, "Old Fast Compare" );

			for ( int i = 0; i < 8; i++ ) {
				nif->set<quint8>( nif->getIndex( oldFastCompare, i ), unk8bytes32[i] );
			}
		} else if ( nif->checkVersion( 0x14000004, 0 ) ) {
			// set stuff
			nif->set<qint32>( iData, "Extra Data", -1 );
			nif->set<quint8>( iData, "Flags", 1 );
			QModelIndex destChannels = nif->getIndex( iData, "Channels" );

			for ( int i = 0; i < 4; i++ ) {
				nif->set<quint32>( nif->getIndex( destChannels, i ), "Type", i );       // red, green, blue, alpha
				nif->set<quint32>( nif->getIndex( destChannels, i ), "Convention", 0 ); // fixed
				nif->set<quint8>( nif->getIndex( destChannels, i ), "Bits Per Channel", 8 );
				nif->set<quint8>( nif->getIndex( destChannels, i ), "Is Signed", 0 );
			}
		}

		nif->set<quint32>( iData, "Num Mipmaps", mipmaps );
		nif->set<quint32>( iData, "Bytes Per Pixel", 4 );
		QModelIndex destMipMaps = nif->getIndex( iData, "Mipmaps" );
		nif->updateArraySize( destMipMaps );

		QByteArray pixelData;

		int mipmapWidth  = width;
		int mipmapHeight = height;
		int mipmapOffset = 0;

		// generate sizes and copy mipmap to NIF
		for ( uint i = 0; i < mipmaps; i++ ) {
			nif->set<quint32>( nif->getIndex( destMipMaps, i ), "Width", mipmapWidth );
			nif->set<quint32>( nif->getIndex( destMipMaps, i ), "Height", mipmapHeight );
			nif->set<quint32>( nif->getIndex( destMipMaps, i ), "Offset", mipmapOffset );

			quint32 mipmapSize = mipmapWidth * mipmapHeight * 4;

			QByteArray mipmapData;
			mipmapData.resize( mipmapSize );

			glGetTexImage( GL_TEXTURE_2D, i, GL_RGBA, GL_UNSIGNED_BYTE, mipmapData.data() );
			//qDebug() << "Copying mipmap" << i << "," << mipmapSize << "bytes";

			pixelData.append( mipmapData );

			//qDebug() << "Now have" << pixelData.size() << "bytes of pixel data";

			// generate next offset, resize
			mipmapOffset += mipmapWidth * mipmapHeight * 4;
			mipmapWidth   = std::max( 1, mipmapWidth / 2 );
			mipmapHeight  = std::max( 1, mipmapHeight / 2 );
		}

		// set total pixel size
		nif->set<quint32>( iData, "Num Pixels", mipmapOffset );

		QModelIndex iPixelData = nif->getIndex( iData, "Pixel Data" );
		nif->updateArraySize( iPixelData );

		nif->set<QByteArray>( iPixelData, "Pixel Data", pixelData );

		// return true once perfected
		//return false;
	} else if ( filepath.endsWith( ".dds", Qt::CaseInsensitive ) ) {
		//qDebug() << "Will copy from DDS data";
		DDS_HEADER ddsHeader;
		char tag[4];
		f.read( &tag[0], 4 );

		if ( strncmp( tag, "DDS ", 4 ) != 0 || f.read( (char *)&ddsHeader, sizeof(DDS_HEADER) ) != sizeof(DDS_HEADER) )
			throw QString( "not a DDS file" );

		qDebug() << "Size: " << ddsHeader.dwSize << "Flags" << ddsHeader.dwHeaderFlags << "Height" << ddsHeader.dwHeight << "Width" << ddsHeader.dwWidth;
		qDebug() << "FourCC:" << ddsHeader.ddspf.dwFourCC;

		if ( ddsHeader.ddspf.dwFlags & DDS_FOURCC ) {
			switch ( ddsHeader.ddspf.dwFourCC ) {
			case FOURCC_DXT1:
				//qDebug() << "DXT1";
				nif->set<uint>( iData, "Pixel Format", 4 );
				break;
			case FOURCC_DXT3:
				//qDebug() << "DXT3";
				nif->set<uint>( iData, "Pixel Format", 5 );
				break;
			case FOURCC_DXT5:
				//qDebug() << "DXT5";
				nif->set<uint>( iData, "Pixel Format", 6 );
				break;
			default:
				qCCritical( nsIo ) << QObject::tr( "Unsupported DDS format: %1 %2" ).arg( ddsHeader.ddspf.dwFourCC ).arg( "FourCC" );
				return false;
				break;
			}
		} else {
			//qDebug() << "RAW";
			switch ( ddsHeader.ddspf.dwRGBBitCount ) {
			case 24:
				// RGB
				nif->set<uint>( iData, "Pixel Format", 0 );
				break;
			case 32:
				// RGBA
				nif->set<uint>( iData, "Pixel Format", 1 );
				break;
			default:
				// theoretically could have a palettised DDS in 8bpp
				qCCritical( nsIo ) << QObject::tr( "Unsupported DDS format: %1 %2" ).arg( ddsHeader.ddspf.dwRGBBitCount ).arg( "BPP" );
				return false;
				break;
			}
		}

		qDebug() << "BPP:" << ddsHeader.ddspf.dwRGBBitCount;
		qDebug() << "RMask:" << ddsHeader.ddspf.dwRBitMask;
		qDebug() << "GMask:" << ddsHeader.ddspf.dwGBitMask;
		qDebug() << "BMask:" << ddsHeader.ddspf.dwBBitMask;
		qDebug() << "AMask:" << ddsHeader.ddspf.dwABitMask;

		// Note that these might not match what's expected; hopefully the loader function is smart
		if ( nif->checkVersion( 0, 0x0A020000 ) ) {
			nif->set<uint>( iData, "Bits Per Pixel", ddsHeader.ddspf.dwRGBBitCount );
			nif->set<uint>( iData, "Red Mask", ddsHeader.ddspf.dwRBitMask );
			nif->set<uint>( iData, "Green Mask", ddsHeader.ddspf.dwGBitMask );
			nif->set<uint>( iData, "Blue Mask", ddsHeader.ddspf.dwBBitMask );
			nif->set<uint>( iData, "Alpha Mask", ddsHeader.ddspf.dwABitMask );

			QModelIndex oldFastCompare = nif->getIndex( iData, "Old Fast Compare" );

			for ( int i = 0; i < 8; i++ ) {
				if ( ddsHeader.ddspf.dwRGBBitCount == 24 ) {
					nif->set<quint8>( nif->getIndex( oldFastCompare, i ), unk8bytes24[i] );
				} else if ( ddsHeader.ddspf.dwRGBBitCount == 32 ) {
					nif->set<quint8>( nif->getIndex( oldFastCompare, i ), unk8bytes32[i] );
				}
			}
		} else if ( nif->checkVersion( 0x14000004, 0 ) ) {
			// set stuff
			nif->set<qint32>( iData, "Extra Data", -1 );
			nif->set<quint8>( iData, "Flags", 1 );
			QModelIndex destChannels = nif->getIndex( iData, "Channels" );

			// DXT1, DXT5
			if ( ddsHeader.ddspf.dwFlags & DDS_FOURCC ) {
				// compressed
				nif->set<uint>( iData, "Bits Per Pixel", 0 );

				for ( int i = 0; i < 4; i++ ) {
					if ( i == 0 ) {
						nif->set<quint32>( nif->getIndex( destChannels, i ), "Type", 4 );       // compressed
						nif->set<quint32>( nif->getIndex( destChannels, i ), "Convention", 4 ); // compressed
					} else {
						nif->set<quint32>( nif->getIndex( destChannels, i ), "Type", 19 );      // empty
						nif->set<quint32>( nif->getIndex( destChannels, i ), "Convention", 5 ); // empty
					}

					nif->set<quint8>( nif->getIndex( destChannels, i ), "Bits Per Channel", 0 );
					nif->set<quint8>( nif->getIndex( destChannels, i ), "Is Signed", 1 );
				}
			} else {
				nif->set<uint>( iData, "Bits Per Pixel", ddsHeader.ddspf.dwRGBBitCount );

				// set RGB mask separately
				for ( int i = 0; i < 3; i++ ) {
					if ( ddsHeader.ddspf.dwRBitMask == RGBA_INV_MASK[i] ) {
						//qDebug() << "red channel" << i;
						nif->set<quint32>( nif->getIndex( destChannels, i ), "Type", 0 );
					} else if ( ddsHeader.ddspf.dwGBitMask == RGBA_INV_MASK[i] ) {
						//qDebug() << "green channel" << i;
						nif->set<quint32>( nif->getIndex( destChannels, i ), "Type", 1 );
					} else if ( ddsHeader.ddspf.dwBBitMask == RGBA_INV_MASK[i] ) {
						//qDebug() << "blue channel" << i;
						nif->set<quint32>( nif->getIndex( destChannels, i ), "Type", 2 );
					}
				}

				for ( int i = 0; i < 3; i++ ) {
					nif->set<quint32>( nif->getIndex( destChannels, i ), "Convention", 0 ); // fixed
					nif->set<quint8>( nif->getIndex( destChannels, i ), "Bits Per Channel", 8 );
					nif->set<quint8>( nif->getIndex( destChannels, i ), "Is Signed", 0 );
				}

				if ( ddsHeader.ddspf.dwRGBBitCount == 32 ) {
					nif->set<quint32>( nif->getIndex( destChannels, 3 ), "Type", 3 );       // alpha
					nif->set<quint32>( nif->getIndex( destChannels, 3 ), "Convention", 0 ); // fixed
					nif->set<quint8>( nif->getIndex( destChannels, 3 ), "Bits Per Channel", 8 );
					nif->set<quint8>( nif->getIndex( destChannels, 3 ), "Is Signed", 0 );
				} else if ( ddsHeader.ddspf.dwRGBBitCount == 24 ) {
					nif->set<quint32>( nif->getIndex( destChannels, 3 ), "Type", 19 );      // empty
					nif->set<quint32>( nif->getIndex( destChannels, 3 ), "Convention", 5 ); // empty
					nif->set<quint8>( nif->getIndex( destChannels, 3 ), "Bits Per Channel", 0 );
					nif->set<quint8>( nif->getIndex( destChannels, 3 ), "Is Signed", 0 );
				}
			}
		}

		// generate mipmap sizes and offsets
		//qDebug() << "Mipmap count: " << ddsHeader.dwMipMapCount;
		nif->set<quint32>( iData, "Num Mipmaps", ddsHeader.dwMipMapCount );
		QModelIndex destMipMaps = nif->getIndex( iData, "Mipmaps" );
		nif->updateArraySize( destMipMaps );

		nif->set<quint32>( iData, "Bytes Per Pixel", ddsHeader.ddspf.dwRGBBitCount / 8 );

		int mipmapWidth  = ddsHeader.dwWidth;
		int mipmapHeight = ddsHeader.dwHeight;
		int mipmapOffset = 0;

		for ( quint32 i = 0; i < ddsHeader.dwMipMapCount; i++ ) {
			nif->set<quint32>( nif->getIndex( destMipMaps, i ), "Width", mipmapWidth );
			nif->set<quint32>( nif->getIndex( destMipMaps, i ), "Height", mipmapHeight );
			nif->set<quint32>( nif->getIndex( destMipMaps, i ), "Offset", mipmapOffset );

			if ( ddsHeader.ddspf.dwFlags & DDS_FOURCC ) {
				if ( ddsHeader.ddspf.dwFourCC == FOURCC_DXT1 ) {
					mipmapOffset += std::max( 8, ( mipmapWidth * mipmapHeight / 2 ) );
				} else if ( ddsHeader.ddspf.dwFourCC == FOURCC_DXT5 ) {
					mipmapOffset += std::max( 16, ( mipmapWidth * mipmapHeight ) );
				}
			} else if ( ddsHeader.ddspf.dwRGBBitCount == 24 ) {
				mipmapOffset += ( mipmapWidth * mipmapHeight * 3 );
			} else if ( ddsHeader.ddspf.dwRGBBitCount == 32 ) {
				mipmapOffset += ( mipmapWidth * mipmapHeight * 4 );
			}

			mipmapWidth  = std::max( 1, mipmapWidth / 2 );
			mipmapHeight = std::max( 1, mipmapHeight / 2 );
		}

		nif->set<quint32>( iData, "Num Pixels", mipmapOffset );

		// finally, copy the data...

		QModelIndex iPixelData = nif->getIndex( iData, "Pixel Data" );
		nif->updateArraySize( iPixelData );

		f.seek( 4 + ddsHeader.dwSize );
		//qDebug() << "Reading from " << f.pos();

		QByteArray ddsData = f.read( mipmapOffset );

		//qDebug() << "Read " << ddsData.size() << " bytes of" << f.size() << ", now at" << f.pos();
		if ( ddsData.size() != mipmapOffset ) {
			qCCritical( nsIo ) << QObject::tr( "Unexpected EOF" );
			return false;
		}

		nif->set<QByteArray>( iPixelData, "Pixel Data", ddsData );

		/*
		QByteArray result = nif->get<QByteArray>( iFaceData, "Pixel Data" );
		for ( int i = 0; i < 16; i++ )
		{
			qDebug() << "Comparing byte " << i << ": result " << (quint8)result[i] << ", original " << (quint8)ddsData[i];
		}
		*/

		// return true once perfected
		//return false;
	}

	return true;
}
