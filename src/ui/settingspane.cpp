#include "settingspane.h"
#include "ui_settingsgeneral.h"
#include "ui_settingsrender.h"
#include "ui_settingsresources.h"

#include "gamemanager.h"

#include "ui/widgets/colorwheel.h"
#include "ui/widgets/floatslider.h"
#include "ui/settingsdialog.h"

#include "nifskope.h"
#include "glview.h"
#include "gl/gltex.h"

#include <QComboBox>
#include <QDebug>
#include <QDir>
#include <QFileDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSettings>
#include <QStringListModel>
#include <QTimer>

using namespace nstheme;
using Game::GameManager;


SettingsPane::SettingsPane( QWidget * parent ) :
	QWidget( parent )
{
	dlg = qobject_cast<SettingsDialog *>(parent);
	if ( dlg ) {
		connect( dlg, &SettingsDialog::loadSettings, this, &SettingsPane::read );
		connect( dlg, &SettingsDialog::saveSettings, this, &SettingsPane::write );
		connect( this, &SettingsPane::paneModified, dlg, &SettingsDialog::modified );
	}

	QSettings settings;
	QVariant settingsVersion = settings.value( "Settings/Version" );
	if ( settingsVersion.isNull() ) {
		// First time install
		setModified( true );
	}
}

SettingsPane::~SettingsPane()
{
}

bool SettingsPane::isModified()
{
	return modified;
}

void SettingsPane::setModified( bool m )
{
	modified = m;
}

void SettingsPane::modifyPane()
{
	if ( !modified ) {
		emit paneModified();

		modified = true;
	}
}

QString humanize( QString str )
{
	str = str.replace( QRegularExpression( "(?<=[A-Z])(?=[A-Z][a-z])|(?<=[^A-Z])(?=[A-Z])|(?<=[A-Za-z])(?=[^A-Za-z])" ), " " );
	str[0] = str[0].toUpper();

	return str;
}


void SettingsPane::readPane( QWidget * w, QSettings & settings )
{
	for ( auto c : w->children() ) {

		auto f = qobject_cast<QFrame *>(c);
		if ( f ) {
			readPane( f, settings );
		}

		auto grp = qobject_cast<QGroupBox *>(c);
		if ( grp ) {
			QString name = grp->title();
			if ( !name.isEmpty() && !grp->isFlat() )
				settings.beginGroup( name );

			readPane( grp, settings );

			if ( !name.isEmpty() && !grp->isFlat() )
				settings.endGroup();
		}

		auto chk = qobject_cast<QCheckBox *>(c);
		if ( chk ) {
			QString name = (!chk->text().isEmpty()) ? chk->text() : ::humanize( chk->objectName() );
			QVariant val = settings.value( name );
			if ( !val.isNull() )
				chk->setChecked( val.toBool() );

#if QT_VERSION < QT_VERSION_CHECK(6, 8, 0)
			connect( chk, &QCheckBox::stateChanged, this, &SettingsPane::modifyPane );
#else
			connect( chk, &QCheckBox::checkStateChanged, this, &SettingsPane::modifyPane );
#endif
		}

		auto cmb = qobject_cast<QComboBox *>(c);
		if ( cmb ) {
			QVariant val = settings.value( ::humanize( cmb->objectName() ) );
			if ( !val.isNull() )
				cmb->setCurrentIndex( val.toInt() );

			connect( cmb, &QComboBox::currentTextChanged, this, &SettingsPane::modifyPane );
		}

		auto btn = qobject_cast<QRadioButton *>(c);
		if ( btn ) {
			QString name = (!btn->text().isEmpty()) ? btn->text() : ::humanize( btn->objectName() );
			QVariant val = settings.value( name );
			if ( !val.isNull() )
				btn->setChecked( val.toBool() );

			connect( btn, &QRadioButton::clicked, this, &SettingsPane::modifyPane );
		}

		auto edt = qobject_cast<QLineEdit *>(c);
		if ( edt ) {
			QVariant val = settings.value( ::humanize( edt->objectName() ) );
			if ( !val.isNull() )
				edt->setText( val.toString() );

			auto tEmit = new QTimer( this );
			tEmit->setInterval( 500 );
			tEmit->setSingleShot( true );
			connect( tEmit, &QTimer::timeout, this, &SettingsPane::modifyPane );
			connect( edt, &QLineEdit::textEdited, [tEmit]() {
				if ( tEmit->isActive() ) {
					tEmit->stop();
				}

				tEmit->start();
			} );
		}

		auto clr = qobject_cast<ColorLineEdit *>(c);
		if ( clr ) {
			QVariant val = settings.value( ::humanize( clr->objectName() ) );
			if ( !val.isNull() )
				clr->setColor( val.value<QColor>() );
		}

		auto spn = qobject_cast<QSpinBox *>(c);
		if ( spn ) {
			QVariant val = settings.value( ::humanize( spn->objectName() ) );
			if ( !val.isNull() )
				spn->setValue( val.toInt() );

			connect( spn, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, &SettingsPane::modifyPane );
		}

		auto dbl = qobject_cast<QDoubleSpinBox *>(c);
		if ( dbl ) {
			QVariant val = settings.value( ::humanize( dbl->objectName() ) );
			if ( !val.isNull() )
				dbl->setValue( val.toDouble() );

			connect( dbl, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), this, &SettingsPane::modifyPane );
		}
	}
}

void SettingsPane::writePane( QWidget * w, QSettings & settings )
{
	for ( auto c : w->children() ) {

		auto f = qobject_cast<QFrame *>(c);
		if ( f ) {
			writePane( f, settings );
		}

		auto grp = qobject_cast<QGroupBox *>(c);
		if ( grp ) {
			QString name = grp->title();
			if ( !name.isEmpty() && !grp->isFlat() )
				settings.beginGroup( name );

			writePane( grp, settings );

			if ( !name.isEmpty() && !grp->isFlat() )
				settings.endGroup();
		}

		auto chk = qobject_cast<QCheckBox *>(c);
		if ( chk ) {
			QString name = (!chk->text().isEmpty()) ? chk->text() : ::humanize( chk->objectName() );
			settings.setValue( name, chk->isChecked() );
		}

		auto cmb = qobject_cast<QComboBox *>(c);
		if ( cmb ) {
			settings.setValue( ::humanize( cmb->objectName() ), cmb->currentIndex() );
		}

		auto btn = qobject_cast<QRadioButton *>(c);
		if ( btn ) {
			QString name = (!btn->text().isEmpty()) ? btn->text() : ::humanize( btn->objectName() );
			settings.setValue( name, btn->isChecked() );
		}

		auto edt = qobject_cast<QLineEdit *>(c);
		if ( edt ) {
			settings.setValue( ::humanize( edt->objectName() ), edt->text() );
		}

		auto clr = qobject_cast<ColorLineEdit *>(c);
		if ( clr ) {
			settings.setValue( ::humanize( clr->objectName() ), clr->getColor() );
		}

		auto spn = qobject_cast<QSpinBox *>(c);
		if ( spn ) {
			settings.setValue( ::humanize( spn->objectName() ), spn->value() );
		}

		auto dbl = qobject_cast<QDoubleSpinBox *>(c);
		if ( dbl ) {
			settings.setValue( ::humanize( dbl->objectName() ), dbl->value() );
		}
	}
}


/*
 * General
 */

SettingsGeneral::SettingsGeneral( QWidget * parent ) :
	SettingsPane( parent ),
	ui( new Ui::SettingsGeneral )
{
	ui->setupUi( this );
	SettingsDialog::registerPage( parent, ui->name->text() );

	QLocale locale( "en" );
	QString txtLang = QLocale::languageToString( locale.language() );

	if ( locale.territory() != QLocale::AnyTerritory )
		txtLang.append( " (" ).append( QLocale::territoryToString( locale.territory() ) ).append( ")" );

	ui->language->addItem( txtLang, locale );
	ui->language->setCurrentIndex( 0 );

	QDir directory( QApplication::applicationDirPath() );

	if ( !directory.cd( "lang" ) ) {
#ifdef Q_OS_LINUX
		directory.cd( "/usr/share/nifskope/lang" );
#endif
	}

	QRegularExpression fileRe( "NifSkope_(.*)\\.qm", QRegularExpression::CaseInsensitiveOption );

	for ( const QString& file : directory.entryList( QStringList( "NifSkope_*.qm" ), QDir::Files | QDir::NoSymLinks ) )
	{
		QRegularExpressionMatch fileReMatch = fileRe.match( file );
		if ( fileReMatch.hasMatch() ) {
			QLocale fileLocale( fileReMatch.capturedTexts()[1] );
			if ( ui->language->findData( fileLocale ) < 0 ) {
				QString txtLang = QLocale::languageToString( fileLocale.language() );

				if ( fileLocale.territory() != QLocale::AnyTerritory )
					txtLang.append( " (" + QLocale::territoryToString( fileLocale.territory() ) + ")" );

				ui->language->addItem( txtLang, fileLocale );
			}
		}
	}

	auto color = [this]( const QString & str, ColorWheel * w, ColorLineEdit * e, const QColor & color ) {
		e->setWheel( w, str );
		w->setColor( color );

		connect( w, &ColorWheel::sigColorEdited, this, &SettingsPane::modifyPane );
		connect( e, &ColorLineEdit::textEdited, this, &SettingsPane::modifyPane );
	};

	auto colors = NifSkope::defaultsDark;

	color( "Base", ui->colorBaseColor, ui->baseColor, colors[Base] );
	color( "Base Alt", ui->colorBaseColorAlt, ui->baseColorAlt, colors[BaseAlt] );
	color( "Text", ui->colorText, ui->text, colors[Text] );
	color( "Base Highlight", ui->colorHighlight, ui->highlight, colors[Highlight] );
	color( "Text Highlight", ui->colorHighlightText, ui->highlightText, colors[HighlightText] );
	color( "Bright Text", ui->colorBrightText, ui->brightText, colors[BrightText] );
}

SettingsGeneral::~SettingsGeneral()
{

}

void SettingsGeneral::read()
{
	QSettings settings;

	settings.beginGroup( "Settings" );

	for ( int i = 0; i < ui->general->count(); i++ ) {

		auto w = ui->general->widget( i );

		settings.beginGroup( ::humanize( w->objectName() ) );
		readPane( w, settings );
		settings.endGroup();
	}

	settings.endGroup();

	setModified( false );
}

void SettingsGeneral::write()
{
	if ( !isModified() )
		return;

	QSettings settings;

	settings.beginGroup( "Settings" );

	int langPrev = settings.value( "UI/Language", 0 ).toInt();

	for ( int i = 0; i < ui->general->count(); i++ ) {

		auto w = ui->general->widget( i );

		settings.beginGroup( ::humanize( w->objectName() ) );
		writePane( w, settings );
		settings.endGroup();
	}

	int langCur = settings.value( "UI/Language", 0 ).toInt();

	settings.endGroup();

	// Set Locale
	auto idx = ui->language->currentIndex();
	settings.setValue( "Settings/Locale", ui->language->itemData( idx ).toLocale() );

	if ( langPrev != langCur ) {
		emit dlg->localeChanged();
	}

	NifSkope::reloadTheme();

	setModified( false );
}

void SettingsGeneral::setDefault()
{
	read();
}


/*
 * Render
 */

SettingsRender::SettingsRender( QWidget * parent ) :
	SettingsPane( parent ),
	ui( new Ui::SettingsRender )
{
	ui->setupUi( this );
	SettingsDialog::registerPage( parent, ui->name->text() );

	connect( ui->btnDetectMSAA, &QPushButton::clicked, this, &SettingsRender::detectMSAAMaxSamples );

	auto color = [this]( const QString & str, ColorWheel * w, ColorLineEdit * e, const QColor & color ) {
		e->setWheel( w, str );
		w->setColor( color );

		connect( w, &ColorWheel::sigColorEdited, this, &SettingsPane::modifyPane );
		connect( e, &ColorLineEdit::textEdited, this, &SettingsPane::modifyPane );
	};

	color( "Background", ui->colorBackground, ui->background, QColor( 46, 46, 46 ) );
	color( "Wireframe", ui->colorWireframe, ui->wireframe, QColor( 0, 255, 0 ) );
	color( "Highlight", ui->colorHighlight, ui->highlight, QColor( 255, 255, 0 ) );
	color( "Grid", ui->colorGrid, ui->gridColor, QColor( 99, 99, 99 ) );


	auto alphaSlider = [this]( ColorWheel * c, ColorLineEdit * e, QHBoxLayout * l, float a = 1.0f ) {
		auto alpha = new AlphaSlider( Qt::Vertical );
		alpha->setParent( this );

		c->setAlpha( true );
		e->setAlpha( a );
		l->addWidget( alpha );

		connect( c, &ColorWheel::sigColor, alpha, &AlphaSlider::setColor );
		connect( alpha, &AlphaSlider::valueChanged, c, &ColorWheel::setAlphaValue );
		connect( alpha, &AlphaSlider::valueChanged, e, &ColorLineEdit::setAlpha );
		connect( alpha, &AlphaSlider::valueChanged, this, &SettingsPane::modifyPane );

		alpha->setColor( e->getColor() );
	};

	alphaSlider( ui->colorWireframe, ui->wireframe, ui->layAlphaWire );
	alphaSlider( ui->colorHighlight, ui->highlight, ui->layAlphaHigh );
	alphaSlider( ui->colorGrid, ui->gridColor, ui->layAlphaGrid, 0.8f );

	connect( ui->btnClearCubeCache, &QPushButton::clicked, this, &SettingsRender::clearCubeCache );
	connect( ui->btnLoadF76CubeMap, &QPushButton::clicked, this, &SettingsRender::selectF76CubeMap );
	connect( ui->btnLoadSTFCubeMap, &QPushButton::clicked, this, &SettingsRender::selectSTFCubeMap );
}

void SettingsRender::read()
{
	QSettings settings;

	settings.beginGroup( "Settings/Render" );

	for ( int i = 0; i < ui->render->count(); i++ ) {

		auto w = ui->render->widget( i );

		settings.beginGroup( ::humanize( w->objectName() ) );
		readPane( w, settings );
		settings.endGroup();
	}

	settings.endGroup();

	setModified( false );
}

void SettingsRender::write()
{
	if ( !isModified() )
		return;

	QSettings settings;

	settings.beginGroup( "Settings/Render" );

	for ( int i = 0; i < ui->render->count(); i++ ) {

		auto w = ui->render->widget( i );

		settings.beginGroup( ::humanize( w->objectName() ) );
		writePane( w, settings );
		settings.endGroup();
	}

	settings.endGroup();

	setModified( false );

	if ( TexCache::loadSettings( settings ) )
		emit dlg->flush3D();
}

SettingsRender::~SettingsRender()
{
}

void SettingsRender::setDefault()
{
	read();
}

void SettingsRender::clearCubeCache()
{
	TexCache::clearCubeCache();
}

void SettingsRender::selectF76CubeMap()
{
	if ( GLView::selectPBRCubeMapForGame( 155 ) )
		modifyPane();
}

void SettingsRender::selectSTFCubeMap()
{
	if ( GLView::selectPBRCubeMapForGame( 172 ) )
		modifyPane();
}

void SettingsRender::detectMSAAMaxSamples()
{
	GLint	maxSamples = -1;
	GLint	maxSamplesI = -1;
	glGetIntegerv( GL_MAX_SAMPLES, &maxSamples );
	glGetIntegerv( GL_MAX_INTEGER_SAMPLES, &maxSamplesI );
	if ( maxSamples <= 0 || ( maxSamplesI > 0 && maxSamplesI < maxSamples ) )
		maxSamples = maxSamplesI;
	if ( maxSamples <= 0 )
		return;

	int	n = std::min< int >( std::bit_width( (unsigned int) maxSamples ) - 1, 4 );
	if ( n != ui->msaaSamples->currentIndex() ) {
		ui->msaaSamples->setCurrentIndex( n );
		modifyPane();
	}
}

/*
 * Resources
 */

static QString lblGame = "lblGame%1";
static QString chkGame = "chkGame%1";
static QString edtGame = "edtGame%1";
static QString btnBrowse = "btnBrowse%1";

SettingsResources::SettingsResources( QWidget * parent ) :
	SettingsPane( parent ),
	ui( new Ui::SettingsResources )
{
	ui->setupUi( this );
	SettingsDialog::registerPage( parent, ui->name->text() );

	folders = new QStringListModel( this );

	ui->foldersList->setModel( folders );

	// TODO: Hide for new asset manager for now
	ui->btnAutoDetectGames->setHidden( true );

	// Populate UI from the GameManager
	manager_sync(true);

	connect( ui->foldersList, &QListView::doubleClicked, this, &SettingsPane::modifyPane );
	connect( ui->chkAlternateExt, &QCheckBox::clicked, this, &SettingsPane::modifyPane );
	connect( ui->chkOtherGamesFallback, &QCheckBox::clicked, this, &SettingsPane::modifyPane );
	connect( ui->chkIgnoreArchiveErrors, &QCheckBox::clicked, this, &SettingsPane::modifyPane );

	// Move Up / Move Down Behavior
	connect( ui->foldersList->selectionModel(), &QItemSelectionModel::currentChanged,
		[this]( const QModelIndex & idx, const QModelIndex & last )
		{
			Q_UNUSED( last );

			ui->btnFolderUp->setEnabled( idx.row() > 0 );
			ui->btnFolderDown->setEnabled( idx.row() < folders->rowCount() - 1 );
		}
	);

	connect( ui->foldersGameList->selectionModel(), &QItemSelectionModel::currentChanged,
		this, &SettingsResources::setFolderList
	);

}

SettingsResources::~SettingsResources()
{
}

void SettingsResources::read()
{
	QSettings settings;

	GameManager::get()->load();

	select_first(ui->foldersGameList);

	setFolderList();

	ui->chkAlternateExt->setChecked( settings.value( "Settings/Resources/Alternate Extensions", false ).toBool() );
	ui->chkOtherGamesFallback->setChecked( settings.value("Settings/Resources/Other Games Fallback", false ).toBool() );
	ui->chkIgnoreArchiveErrors->setChecked( settings.value("Settings/Resources/Ignore Archive Errors", false ).toBool() );

	setModified( false );
}

void SettingsResources::write()
{
	if ( !isModified() )
		return;

	GameManager::close_resources();
	auto mgr = GameManager::get();
	mgr->save();

	QSettings settings;
	settings.setValue( "Settings/Resources/Alternate Extensions", ui->chkAlternateExt->isChecked() );
	settings.setValue( "Settings/Resources/Other Games Fallback", ui->chkOtherGamesFallback->isChecked() );

	setModified( false );

	emit dlg->flush3D();
}

void SettingsResources::manager_sync( bool make_connections )
{
	for ( int i = 0; i < Game::NUM_GAMES; i++ ) {
		// Get each widget for game slot `i`
		auto lbl = findChild<QLabel *>(lblGame.arg(i));
		auto chk = findChild<QCheckBox *>(chkGame.arg(i));
		auto edt = findChild<QLineEdit *>(edtGame.arg(i));
		auto btn = findChild<QPushButton *>(btnBrowse.arg(i));

		auto game = Game::GameMode(i);
		auto game_string = Game::StringForMode(game);
		if ( game_string.isEmpty() || !lbl || !chk || !edt || !btn )
			continue;

		// Rename generic `GAME_N` label to game name
		lbl->setText(game_string);

		// Sync line edit with game install path if it exists
		auto path_string = GameManager::path(game);
		if ( !path_string.isNull() )
			edt->setText(path_string);
		edt->setEnabled( GameManager::status(game) );
		btn->setEnabled( GameManager::status(game) );

		// Sync Enabled checkbox
		chk->setChecked( GameManager::status(game) );

		// Rename all GAME_%1 list items to the supported game at that position
		auto game_txt = QString("GAME_%1");
		auto folder_item = ui->foldersGameList->findItems(game_txt.arg(i), Qt::MatchExactly).value(0, nullptr);
		if ( folder_item ) {
			folder_item->setText(game_string);
			// Hide game items from Folders list when game is disabled
			folder_item->setHidden(!chk->isChecked());
			if ( make_connections ) {
				connect(chk, &QCheckBox::clicked, [chk, folder_item]() {
					folder_item->setHidden(!chk->isChecked());
				});
			}
		}

		if ( make_connections ) {
			connect(btn, &QPushButton::clicked, this, &SettingsResources::onBrowseClicked);
			connect(chk, &QCheckBox::clicked, this, &SettingsPane::modifyPane);
			connect(chk, &QCheckBox::clicked, [btn, chk, edt, game]() {
				GameManager::update_status( game, chk->isChecked() );
				edt->setEnabled( chk->isChecked() );
				btn->setEnabled( chk->isChecked() );
			});
		}
	}
}

void SettingsResources::setDefault()
{
	read();
}

void SettingsResources::setFolderList()
{
	folders->setStringList(GameManager::folders(currentFolderItem()));
	ui->foldersList->setCurrentIndex( folders->index( 0, 0 ) );
}

void SettingsResources::select_first( QListWidget * list )
{
	for ( int i = 0; i < list->count(); i++ ) {
		// Select the first visible item
		if ( !list->item(i)->isHidden() ) {
			list->setCurrentRow(i);
			break;
		}
	}
}

void SettingsResources::onBrowseClicked()
{
	QPushButton * btn = qobject_cast<QPushButton *>(sender());
	if ( !btn )
		return;

	QFileDialog dialog(this);
	dialog.setFileMode(QFileDialog::Directory);

	QString path;
	if ( dialog.exec() )
		path = dialog.selectedFiles().at(0);

	auto edt = ui->resourcesGames->findChild<QLineEdit *>(btn->objectName().replace("btnBrowse", "edtGame"));
	if ( edt ) {
		if ( path.isEmpty() )
			edt->clear();
		else
			edt->setText(path);
	}

	auto lbl = ui->resourcesGames->findChild<QLabel *>(btn->objectName().replace("btnBrowse", "lblGame"));
	if ( lbl ) {
		GameManager::update_game(lbl->text(), path);
	}
	SettingsPane::modifyPane();
}

QString SettingsResources::currentFolderItem()
{
	if ( !ui->foldersGameList->currentItem() )
		return {};
	return ui->foldersGameList->currentItem()->text();
}

void SettingsResources::modifyPane()
{
	GameManager::update_folders( currentFolderItem(), folders->stringList() );
	GameManager::update_other_games_fallback( ui->chkOtherGamesFallback->isChecked() );
	GameManager::update_ignore_archive_errors( ui->chkIgnoreArchiveErrors->isChecked() );
	SettingsPane::modifyPane();
}

void moveIdxDown( QListView * view )
{
	QModelIndex idx = view->currentIndex();
	auto model = view->model();

	if ( idx.isValid() && idx.row() < model->rowCount() - 1 ) {
		QModelIndex downIdx = idx.sibling( idx.row() + 1, 0 );
		QVariant v = model->data( idx, Qt::EditRole );
		model->setData( idx, model->data( downIdx, Qt::EditRole ) );
		model->setData( downIdx, v );
		view->setCurrentIndex( downIdx );
	}
}

void moveIdxUp( QListView * view )
{
	QModelIndex idx = view->currentIndex();
	auto model = view->model();

	if ( idx.isValid() && idx.row() > 0 ) {
		QModelIndex upIdx = idx.sibling( idx.row() - 1, 0 );
		QVariant v = model->data( idx, Qt::EditRole );
		model->setData( idx, model->data( upIdx, Qt::EditRole ) );
		model->setData( upIdx, v );
		view->setCurrentIndex( upIdx );
	}
}

void SettingsResources::on_btnFolderAdd_clicked()
{
	QFileDialog dialog( this );
	dialog.setFileMode( QFileDialog::Directory );
	dialog.setDirectory(GameManager::data(currentFolderItem()));

	QString path;
	if ( dialog.exec() )
		path = dialog.selectedFiles().at( 0 );

	if ( path.isEmpty() )
		return;

	folders->insertRow( 0 );
	folders->setData( folders->index( 0, 0 ), path );
	ui->foldersList->setCurrentIndex( folders->index( 0, 0 ) );
	modifyPane();
}

void SettingsResources::on_btnArchiveAdd_clicked()
{
	QFileDialog	dialog( this );
	dialog.setFileMode( QFileDialog::ExistingFiles );
	dialog.setNameFilter( "Supported file types (*.ba2 *.bsa *.nif *.bto *.btr *.mesh *.bgsm *.bgem *.cdb *.mat *.dds)" );
	dialog.setDirectory( GameManager::data(currentFolderItem()) );

	QStringList	paths;
	if ( dialog.exec() )
		paths = dialog.selectedFiles();
	if ( paths.size() < 1 )
		return;

	for ( qsizetype i = paths.size(); i-- > 0; ) {
		folders->insertRow( 0 );
		folders->setData( folders->index( 0, 0 ), paths.at( i ) );
		ui->foldersList->setCurrentIndex( folders->index( 0, 0 ) );
	}
	modifyPane();
}

void SettingsResources::on_btnFolderRemove_clicked()
{
	ui->foldersList->model()->removeRow( ui->foldersList->currentIndex().row() );
	modifyPane();
}

void SettingsResources::on_btnFolderDown_clicked()
{
	moveIdxDown( ui->foldersList );
	modifyPane();
}

void SettingsResources::on_btnFolderUp_clicked()
{
	moveIdxUp( ui->foldersList );
	modifyPane();
}

void SettingsResources::on_btnFolderAutoDetect_clicked()
{
	QStringList folders_list = folders->stringList();
	for ( const auto& f : GameManager::find_paths(currentFolderItem()) ) {
		qsizetype	i;
		while ( ( i = folders_list.indexOf( f, Qt::CaseInsensitive ) ) >= 0 )
			folders_list.removeAt( i );
		folders_list << f;
	}

	folders_list.removeDuplicates();

	folders->setStringList(folders_list);

	ui->foldersList->setCurrentIndex(folders->index(0, 0));

	modifyPane();
}
