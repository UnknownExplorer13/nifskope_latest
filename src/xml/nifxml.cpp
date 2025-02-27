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

#include "xmlconfig.h"
#include "message.h"
#include "data/niftypes.h"
#include "model/nifmodel.h"

#include <QCoreApplication>
#include <QDir>
#include <QMessageBox>
#include <QXmlStreamReader>


//! \file nifxml.cpp NifXmlHandler, NifModel XML

//! Set NifXmlHandler::errorStr and return
#define err( X ) { errorStr = X; return; }

QReadWriteLock             NifModel::XMLlock;
QList<quint32>             NifModel::supportedVersions;
QHash<QString, NifBlockPtr> NifModel::compounds;
QHash<QString, NifBlockPtr> NifModel::fixedCompounds;
QHash<QString, NifBlockPtr> NifModel::blocks;
QMap<quint32, NifBlockPtr> NifModel::blockHashes;


// Current token attribute list
QString attrlist;
// Token storage
QMap<QString, QVector<QPair<QString, QString>>> tokens;

//! Parses nif.xml
class NifXmlHandler final
{
//	Q_DECLARE_TR_FUNCTIONS(NifXmlHandler)

public:
	//! XML block types
	enum Tag
	{
		tagNone = 0,
		tagFile,
		tagVersion,
		tagCompound,
		tagBlock,
		tagAdd,
		tagAddDefault,
		tagBasic,
		tagEnum,
		tagOption,
		tagBitFlag,
		tagBitfield,
		tagMember,
		tagToken,
		tagTokenTag,
		tagModule,
		tagVerAttr
	};

	//! i18n wrapper for various strings
	/*!
	 * Note that we don't use QObject::tr() because that doesn't provide
	 * context. We also don't use the QCoreApplication Q_DECLARE_TR_FUNCTIONS()
	 * macro because that won't document properly.
	 */
	static inline QString tr( const char * key, const char * comment = 0 ) { return QCoreApplication::translate( "NifXmlHandler", key, comment ); }

	//! Constructor
	NifXmlHandler()
	{
		tags.insert( "niftoolsxml", tagFile );
		tags.insert( "version", tagVersion );
		tags.insert( "module", tagModule );
		tags.insert( "struct", tagCompound );
		tags.insert( "niobject", tagBlock );
		tags.insert( "field", tagAdd );
		tags.insert( "default", tagAddDefault );
		tags.insert( "basic", tagBasic );
		tags.insert( "enum", tagEnum );
		tags.insert( "option", tagOption );
		tags.insert( "bitflags", tagBitFlag );
		tags.insert( "token", tagToken );
		tags.insert( "bitfield", tagBitfield );
		tags.insert( "member", tagMember );
		tags.insert( "verattr", tagVerAttr );

		tokens.clear();
	}

	//! Current position on stack
	int depth = 0;
	//! Tag stack
	Tag stack[10] = {};
	//! Hashmap of tags
	QHash<QString, Tag> tags;
	//! Error string
	QString errorStr;

	//! Current type ID
	QString typId;
	//! Current type description
	QString typTxt;

	//! Current enumeration ID
	QString optId;
	//! Current enumeration value
	QString optVal;
	//! Current enumeration bit
	QString optBit;
	//! Current enumeration text
	QString optTxt;

	//! Block
	NifBlockPtr blk = nullptr;
	//! Data
	NifData data;

	//! The current tag
	Tag current() const
	{
		return stack[depth - 1];
	}
	//! Add a tag to the stack
	void push( Tag x )
	{
		stack[depth++] = x;
	}
	//! Get a tag from the stack
	Tag pop()
	{
		return stack[--depth];
	}

	QString token_replace( const QXmlStreamAttributes & list, const QString & attr )
	{
		QString str = list.value( attr ).toString();
		if ( tokens.contains( attr ) ) {
			for ( const auto& p : tokens[attr] )
				if ( p.second == "INFINITY" )
					str.replace(p.first, "0x7F800000");
				else
					str.replace( p.first, p.second );
		}
		return str;
	}

	/*!
	 * \param tagid Qualified name
	 * \param list Attributes
	 */
	void startElement( const QString & tagid, const QXmlStreamAttributes & list )
	{
		if ( depth >= 8 )
			err( tr( "error maximum nesting level exceeded" ) );

		Tag x = tags.value( tagid );

		// Token Replace lambda, use get(x) instead of list.value(x)
		auto get = [this, &list]( const QString & attr ) {
			return token_replace( list, attr );
		};

		if ( x == tagToken )
			tags.insert( list.value( QLatin1StringView("name") ).toString(), tagTokenTag );

		if ( x == tagNone ) {
			x = tags.value( tagid );
			if ( x == tagNone )
				err( tr( "error unknown element '%1'" ).arg( tagid ) );
		}

		if ( depth == 0 ) {
			if ( x != tagFile )
				err( tr( "this is not a niftoolsxml file" ) );

			push( x );
			return;
		}

		switch ( current() ) {
		case tagFile:
			push( x );

			switch ( x ) {
			case tagCompound:
			case tagBlock:
				{
					QString name = list.value( QLatin1StringView("name") ).toString();

					if ( NifValue::type( name ) != NifValue::tNone ) {
						// Internal Type
						typId  = name;
						typTxt = QString();
					} else {
						QString id = name;

						if ( x == tagCompound && NifValue::isValid( NifValue::type( id ) ) )
							err( tr( "struct %1 is already registered as internal type" ).arg( list.value( QLatin1StringView("name") ) ) );

						if ( id.isEmpty() )
							err( tr( "struct and niblocks must have a name" ) );

						if ( NifModel::compounds.contains( id ) || NifModel::blocks.contains( id ) )
							err( tr( "multiple declarations of %1" ).arg( id ) );

						if ( !blk )
							blk = NifBlockPtr( new NifBlock );

						blk->id = id;
						if ( auto v = list.value( QLatin1StringView("abstract") ); true )
							blk->abstract = ( v == QLatin1StringView("1") || v == QLatin1StringView("true") );

						if ( x == tagBlock ) {
							blk->ancestor = list.value( QLatin1StringView("inherit") ).toString();

							if ( !blk->ancestor.isEmpty() ) {
								if ( !NifModel::blocks.contains( blk->ancestor ) )
									err( tr( "forward declaration of block id %1" ).arg( blk->ancestor ) );
							}
						}

						QString externalCond = list.value( QLatin1StringView("externalcond") ).toString();
						if ( externalCond == "1" || blk->id.startsWith( "BSVertexData" ) ) {
							NifModel::fixedCompounds.insert( blk->id, blk );
						}
					}
				}
				break;
			case tagBasic:
				{
					QString name = list.value( QLatin1StringView("name") ).toString();

					if ( NifValue::type( name ) == NifValue::tNone )
						err( tr( "basic definition %1 must have an internal NifSkope type" ).arg( name ) );

					typId = name;
					typTxt = QString();
				}
				break;
			case tagEnum:
			case tagBitFlag:
				{
					typId  = list.value( QLatin1StringView("name") ).toString();
					typTxt = QString();
					QString storage = list.value( QLatin1StringView("storage") ).toString();

					if ( typId.isEmpty() || storage.isEmpty() )
						err( tr( "enum definition must have a name and a known storage type" ) );

					if ( !NifValue::registerAlias( typId, storage ) )
						err( tr( "failed to register alias %1 for enum type %2" ).arg( storage, typId ) );

					NifValue::EnumType flags = (x == tagBitFlag) ? NifValue::eFlags : NifValue::eDefault;
					NifValue::registerEnumType( typId, flags );
				}
				break;
			case tagBitfield:
				{
					typId = list.value( QLatin1StringView("name") ).toString();
					typTxt = QString();
					QString storage = list.value( QLatin1StringView("storage") ).toString();

					if ( typId.isEmpty() || storage.isEmpty() )
						err( tr( "bitfield definition must have a name and a known storage type" ) );

					if ( !NifValue::registerAlias( typId, storage ) )
						err( tr( "failed to register alias %1 for enum type %2" ).arg( storage, typId ) );
				}
				break;
			case tagVersion:
				{
					int v = NifModel::version2number( list.value( QLatin1StringView("num") ).toString().trimmed() );

					if ( v != 0 && !list.value( QLatin1StringView("num") ).isEmpty() )
						NifModel::supportedVersions.append( v );
					else
						err( tr( "invalid version tag" ) );
				}
				break;
			case tagToken:
				{
					attrlist = list.value( QLatin1StringView("attrs") ).toString();
				}
				break;
			case tagModule:
			case tagVerAttr: // Unused Metadata
				break;
			default:
				err( tr( "expected basic, enum, struct, niobject or version got %1 instead" ).arg( tagid ) );
			}

			break;
		case tagVersion:
			//err( tr("version tag must not contain any sub tags") );
			break;
		case tagCompound:

			if ( x != tagAdd )
				err( tr( "only field tags allowed in struct type declaration" ) );
			[[fallthrough]];

		case tagBlock:
			push( x );

			switch ( x ) {
			case tagAdd:
				{
					QString type = list.value( QLatin1StringView("type") ).toString();
					QString tmpl = list.value( QLatin1StringView("template") ).toString();
					QString arg  = get( "arg" );
					QString arr1 = get( "length" );
					QString arr2 = get( "width" );
					QString cond = get( "cond" );
					QString ver1 = list.value( QLatin1StringView("since") ).toString();
					QString ver2 = list.value( QLatin1StringView("until") ).toString();
					QString abs = list.value( QLatin1StringView("abstract") ).toString();
					QString bin = list.value( QLatin1StringView("binary") ).toString();
					QString vercond = get( "vercond" );
					QString defval = get( "default" );

					bool hasTypeCondition = false;

					QString onlyT = list.value( QLatin1StringView("onlyT") ).toString();
					QString excludeT = list.value( QLatin1StringView("excludeT") ).toString();
					if ( !onlyT.isEmpty() || !excludeT.isEmpty() ) {
						Q_ASSERT( cond.isEmpty() );
						Q_ASSERT( onlyT.isEmpty() != excludeT.isEmpty() );
						// hasTypeCondition flag here relies on that cond is always either "cond" attribute or "onlyT/excludeT", not a combination of both at the same time.
						// If it ever changes, hasTypeCondition logic will have to be rewritten.
						hasTypeCondition = true;
						if ( !onlyT.isEmpty() )
							cond = onlyT;
						else
							cond = QString( "!%1" ).arg( excludeT );
					}


					bool isTemplated = (type == XMLTMPL || tmpl == XMLTMPL);
					bool isCompound = NifModel::compounds.contains( type );
					bool isArray = !arr1.isEmpty();
					bool isMultiArray = !arr2.isEmpty();
					if ( isMultiArray && !isArray )
						err( tr("\"width\" attribute without \"length\" attribute") );

					// Override some compounds as mixins (compounds without nesting)
					//	This flattens the hierarchy as if the mixin's <add> rows belong to the mixin's parent
					//	This only takes place on a per-row basis and reverts to nesting when in an array.
					bool isMixin = false;
					if ( isCompound ) {
						static const QVector<QString> mixinTypes {
							"HavokFilter",
							"HavokMaterial",
							"bhkRagdollConstraintCInfo",
							"bhkLimitedHingeConstraintCInfo",
							"bhkHingeConstraintCInfo",
							"bhkBallAndSocketConstraintCInfo",
							"bhkPrismaticConstraintCInfo",
							"bhkMalleableConstraintCInfo",
							"bhkConstraintData",
							"bhkConstraintCInfo"
						};

						isMixin = mixinTypes.contains( type );
						// The <add> must not use any attributes other than name/type
						isMixin = isMixin && !isTemplated && !isArray;
						isMixin = isMixin && cond.isEmpty() && ver1.isEmpty() && ver2.isEmpty() && vercond.isEmpty();

						isCompound = !isMixin;
					}

					// Special casing for BSVertexDesc as an ARG
					// since we have internalized the type
					if ( arg == "Vertex Desc\\Vertex Attributes" )
						arg = "Vertex Desc";

					// now allocate
					data = NifData(
						list.value( QLatin1StringView("name") ).toString(),
						type,
						tmpl,
						NifValue( NifValue::type( type ) ),
						arg,
						arr1,
						arr2,
						cond,
						NifModel::version2number( ver1 ),
						NifModel::version2number( ver2 )
					);

					// Set data flags
					data.setAbstract( abs == "1" || abs == "true" );
					data.setBinary( bin == "1" || bin == "true" );
					data.setTemplated( isTemplated );
					data.setIsCompound( isCompound );
					data.setIsArray( isArray );
					data.setIsMultiArray( isMultiArray );
					data.setIsMixin( isMixin );
					data.setHasTypeCondition( hasTypeCondition );

					if ( data.isBinary() && isMultiArray )
						err( tr("Binary multi-arrays not supported") );

					if ( !defval.isEmpty() ) {
						bool ok;
						quint32 enumVal = NifValue::enumOptionValue( type, defval, &ok );

						if ( ok ) {
							data.setCount( enumVal, nullptr, nullptr );
						} else {
							data.setFromString( defval, nullptr, nullptr );
						}
					}

					if ( !vercond.isEmpty() ) {
						data.setVerCond( vercond );
					}

					// Set conditionless flag on data
					if ( cond.isEmpty() && vercond.isEmpty() && ver1.isEmpty() && ver2.isEmpty() )
						data.setIsConditionless( true );

					if ( data.name().isEmpty() || data.type().isEmpty() )
						err( tr( "field needs at least name and type attributes" ) );
				}
				break;
			default:
				err( tr( "only field tags allowed in block declaration" ) );
			}

			break;
		case tagAdd:
			// Member child tags
			push( x );
			switch ( x ) {
			case tagAddDefault:
				// Subclass defaults
				break;
			default:
				break;
			}
			break;
		case tagEnum:
		case tagBitFlag:
			push( x );

			switch ( x ) {
			case tagOption:
				optId  = list.value( QLatin1StringView("name") ).toString();
				optVal = list.value( QLatin1StringView("value") ).toString();
				optBit = list.value( QLatin1StringView("bit") ).toString();
				if ( !optBit.isEmpty() )
					optVal = optBit;

				optTxt = QString();

				if ( optId.isEmpty() || optVal.isEmpty() )
					err( tr( "option defintion must have a name and a value" ) );

				bool ok;
				optVal.toUInt( &ok, 0 );

				if ( !ok )
					err( tr( "option value error (only integers please)" ) );

				break;
			default:
				err( tr( "only option tags allowed in enum declaration" ) );
			}

			break;
		case tagBitfield:
			push( x );
			switch ( x ) {
			case tagMember:
				break;
			default:
				err( tr( "only member tags allowed in bitfield declaration" ) );
			}
			break;
		case tagToken:
			push( x );
			switch ( x ) {
			case tagTokenTag:
				{
					auto tok = list.value( QLatin1StringView("token") ).toString();
					auto str = list.value( QLatin1StringView("string") ).toString();
					for ( const auto & attr : attrlist.split( " " ) ) {
						if ( !tokens.contains(attr) )
							tokens[attr] = {};

						tokens[attr].append( {tok, str} );
					}
				}
				break;
			default:
				err( tr( "only token tags allowed in token declaration" ) );;
			}
			break;
		case tagVerAttr:
			// Unused Metadata
			break;
		default:
			err( tr( "error unhandled tag %1" ).arg( tagid ) );
			break;
		}
	}

	/*!
	 * \param tagid Qualified name
	 */
	void endElement( const QString & tagid )
	{
		if ( depth <= 0 )
			err( tr( "mismatching end element tag for element %1" ).arg( tagid ) );

		Tag x = tags.value( tagid );

		if ( pop() != x )
			err( tr( "mismatching end element tag for element %1" ).arg( tagid ) );

		switch ( x ) {
		case tagCompound:
			if ( blk && !blk->id.isEmpty() && !blk->text.isEmpty() )
				NifValue::setTypeDescription( blk->id, blk->text );
			else if ( !typId.isEmpty() && !typTxt.isEmpty() )
				NifValue::setTypeDescription( typId, typTxt );
			[[fallthrough]];

		case tagBlock:
			if ( blk ) {
				if ( blk->id.isEmpty() ) {
					blk = nullptr;
					err( tr( "invalid %1 declaration: name is empty" ).arg( tagid ) );
				}

				switch ( x ) {
				case tagCompound:
					NifModel::compounds.insert( blk->id, blk );
					break;
				case tagBlock:
					NifModel::blocks.insert( blk->id, blk );
					NifModel::blockHashes.insert( DJB1Hash(blk->id.toStdString().c_str()), blk );
					break;
				default:
					break;
				}

				blk = 0;
			}

			break;
		case tagAdd:
			if ( blk )
				blk->types.append( data );

			break;
		case tagOption:
			{
				bool ok;
				quint32 optValInt = optVal.toUInt( &ok, 0 );

				if ( !ok || !NifValue::registerEnumOption( typId, optId, optValInt, optTxt ) )
					err( tr( "failed to register enum option" ) );
			}
			break;
		case tagBasic:
		case tagEnum:
		case tagBitFlag:
			NifValue::setTypeDescription( typId, typTxt );
		default:
			break;
		}
	}

	/*!
	 * \param s The character data
	 */
	void characters( const QString & s )
	{
		switch ( current() ) {
		case tagVersion:
			break;
		case tagCompound:
		case tagBlock:
			if ( blk )
				blk->text += s.trimmed();
			else
				typTxt += s.trimmed();

			break;
		case tagAdd:
			data.setText( data.text() + s.trimmed() );
			break;
		case tagBasic:
		case tagEnum:
		case tagBitFlag:
			typTxt += s.trimmed();
			break;
		case tagOption:
			optTxt += s.trimmed();
			break;
		default:
			break;
		}
	}

	//! Checks that the type of the data is valid
	bool checkType( const NifData & d )
	{
		return ( NifModel::compounds.contains( d.type() )
				|| NifValue::type( d.type() ) != NifValue::tNone
				|| d.type() == XMLTMPL
		);
	}

	//! Checks that a template type is valid
	bool checkTemp( const NifData & d )
	{
		return ( d.templ().isEmpty()
				|| NifValue::type( d.templ() ) != NifValue::tNone
				|| d.templ() == XMLTMPL
				|| NifModel::blocks.contains( d.templ() )
				|| NifModel::compounds.contains( d.templ() )
		);
	}

	void endDocument()
	{
		// make a rough check of the maps
		for ( const QString& key : NifModel::compounds.keys() ) {
			NifBlockPtr c = NifModel::compounds.value( key );
			for ( NifData data :c->types ) {
				if ( !checkType( data ) )
					err( tr( "struct type %1 refers to unknown type %2" ).arg( key, data.type() ) );

				if ( !checkTemp( data ) )
					err( tr( "struct type %1 refers to unknown template type %2" ).arg( key, data.templ() ) );

				if ( data.type() == key )
					err( tr( "struct type %1 contains itself" ).arg( key ) );
			}
		}

		for ( const QString& key : NifModel::blocks.keys() ) {
			NifBlockPtr b = NifModel::blocks.value( key );

			if ( !b->ancestor.isEmpty() && !NifModel::blocks.contains( b->ancestor ) )
				err( tr( "niobject %1 inherits unknown ancestor %2" ).arg( key, b->ancestor ) );

			if ( b->ancestor == key )
				err( tr( "niobject %1 inherits itself" ).arg( key ) );

			for ( const NifData& data : b->types ) {
				if ( !checkType( data ) )
					err( tr( "niobject %1 refers to unknown type %2" ).arg( key, data.type() ) );

				if ( !checkTemp( data ) )
					err( tr( "niobject %1 refers to unknown template type %2" ).arg( key, data.templ() ) );
			}
		}
	}

	QString errorString() const
	{
		return errorStr;
	}
};

// documented in nifmodel.h
bool NifModel::loadXML()
{
	QDir        dir( QCoreApplication::applicationDirPath() );
	QString     fname;
	QStringList xmlList( QStringList()
						 << "nif.xml"
#ifdef Q_OS_LINUX
						 << "/usr/share/nifskope/nif.xml"
#endif
#ifdef Q_OS_MACOS
						 << "../../../nif.xml"
#endif
	);
	for ( const QString& str : xmlList ) {
		if ( dir.exists( str ) ) {
			fname = dir.filePath( str );
			break;
		}
	}
	QString result = NifModel::parseXmlDescription( fname );

	if ( !result.isEmpty() ) {
		Message::append( tr( "<b>Error loading XML</b><br/>You will need to reinstall the XML and restart the application." ), result, QMessageBox::Critical );
		return false;
	}

	return true;
}

// documented in nifmodel.h
QString NifModel::parseXmlDescription( const QString & filename )
{
	QWriteLocker lck( &XMLlock );

	compounds.clear();
	blocks.clear();

	supportedVersions.clear();

	NifValue::initialize();

	QFile f( filename );

	if ( !f.exists() )
		return tr( "nif.xml could not be found. Please install it and restart the application." );

	if ( !f.open( QIODevice::ReadOnly | QIODevice::Text ) )
		return tr( "Couldn't open NIF XML description file: %1" ).arg( filename );

	NifXmlHandler handler;
	QXmlStreamReader reader( &f );
	while ( !reader.atEnd() && handler.errorStr.isEmpty() ) {
		reader.readNext();
		if ( reader.isStartElement() )
			handler.startElement( reader.name().toString(), reader.attributes() );
		else if ( reader.isEndElement() )
			handler.endElement( reader.name().toString() );
		else if ( reader.isCharacters() )
			handler.characters( reader.text().toString() );
		else if ( reader.isEndDocument() )
			handler.endDocument();
	}

	QString	errorStr;
	if ( reader.hasError() ) {
		errorStr = reader.errorString();
		if ( errorStr.isEmpty() )
			errorStr = tr( "Syntax error" );
	} else {
		errorStr = handler.errorString();
	}

	if ( !errorStr.isEmpty() ) {
		errorStr.prepend( tr( "%1 XML parse error (line %2): " ).arg( "NIF" ).arg( reader.lineNumber() ) );

		compounds.clear();
		blocks.clear();
		supportedVersions.clear();
	}

	return errorStr;
}

