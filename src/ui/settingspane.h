#ifndef SETTINGSPANE_H
#define SETTINGSPANE_H

#include <QWidget>
#include <QSettings>

#include <memory>


//! @file settingspane.h SettingsPane, SettingsGeneral, SettingsRender, SettingsResources

class SettingsDialog;
class QStringListModel;
class QListWidget;

namespace Ui {
class SettingsGeneral;
class SettingsRender;
class SettingsResources;
}

class SettingsPane : public QWidget
{
	Q_OBJECT

public:
	explicit SettingsPane( QWidget * parent = nullptr );
	virtual ~SettingsPane();

	virtual void read() = 0;
	virtual void write() = 0;
	virtual void setDefault() = 0;

public slots:
	virtual void modifyPane();

signals:
	void paneModified();

protected:
	void readPane( QWidget * w, QSettings & settings );
	void writePane( QWidget * w, QSettings & settings );
	bool isModified();
	void setModified( bool );

	SettingsDialog * dlg;

	bool modified = false;
};

class SettingsGeneral : public SettingsPane
{
	Q_OBJECT

public:
	explicit SettingsGeneral( QWidget * parent = nullptr );
	~SettingsGeneral();

	void read() override final;
	void write() override final;
	void setDefault() override final;

private:
	std::unique_ptr<Ui::SettingsGeneral> ui;
};

class SettingsRender : public SettingsPane
{
	Q_OBJECT

public:
	explicit SettingsRender( QWidget * parent = nullptr );
	~SettingsRender();

	void read() override final;
	void write() override final;
	void setDefault() override final;

public slots:
	void clearCubeCache();
	void selectF76CubeMap();
	void selectSTFCubeMap();
	void detectMSAAMaxSamples();

private:
	std::unique_ptr<Ui::SettingsRender> ui;
};

class SettingsResources : public SettingsPane
{
	Q_OBJECT

public:
	explicit SettingsResources( QWidget * parent = nullptr );
	~SettingsResources();

	void read() override final;
	void write() override final;
	void setDefault() override final;

	void select_first(QListWidget* list);
	void manager_sync(bool make_connections = false);

public slots:
	void modifyPane() override;
	void on_btnFolderAdd_clicked();
	void on_btnArchiveAdd_clicked();
	void on_btnFolderRemove_clicked();
	void on_btnFolderDown_clicked();
	void on_btnFolderUp_clicked();
	void on_btnFolderAutoDetect_clicked();

	void setFolderList();

	void onBrowseClicked();

private:
	std::unique_ptr<Ui::SettingsResources> ui;

	QString currentFolderItem();

	QStringListModel * folders;

	QStringList applicableFolders{ "materials", "textures", "geometries" };
};


#endif // SETTINGSPANE_H
