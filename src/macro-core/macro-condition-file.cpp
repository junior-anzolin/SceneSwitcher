#include "macro-condition-edit.hpp"
#include "macro-condition-file.hpp"
#include "utility.hpp"
#include "advanced-scene-switcher.hpp"
#include "curl-helper.hpp"

#include <QTextStream>
#include <QFileDialog>
#include <regex>

const std::string MacroConditionFile::id = "file";

bool MacroConditionFile::_registered = MacroConditionFactory::Register(
	MacroConditionFile::id,
	{MacroConditionFile::Create, MacroConditionFileEdit::Create,
	 "AdvSceneSwitcher.condition.file"});

static std::hash<std::string> strHash;

static size_t WriteCallback(void *contents, size_t size, size_t nmemb,
			    void *userp)
{
	((std::string *)userp)->append((char *)contents, size * nmemb);
	return size * nmemb;
}

static std::string getRemoteData(std::string &url)
{
	std::string readBuffer;
	switcher->curl.SetOpt(CURLOPT_URL, url.c_str());
	switcher->curl.SetOpt(CURLOPT_WRITEFUNCTION, WriteCallback);
	switcher->curl.SetOpt(CURLOPT_WRITEDATA, &readBuffer);
	// Set timeout to at least one second
	int timeout = switcher->interval / 1000;
	if (timeout == 0) {
		timeout = 1;
	}
	switcher->curl.SetOpt(CURLOPT_TIMEOUT, 1);
	switcher->curl.Perform();
	return readBuffer;
}

bool MacroConditionFile::MatchFileContent(QString &filedata)
{
	if (_onlyMatchIfChanged) {
		size_t newHash = strHash(filedata.toUtf8().constData());
		if (newHash == _lastHash) {
			return false;
		}
		_lastHash = newHash;
	}

	if (_regex.Enabled()) {
		auto expr = _regex.GetRegularExpression(_text);
		if (!expr.isValid()) {
			return false;
		}
		auto match = expr.match(filedata);
		return match.hasMatch();
	}

	QString text = QString::fromStdString(_text);
	return compareIgnoringLineEnding(text, filedata);
}

bool MacroConditionFile::CheckRemoteFileContent()
{
	std::string data = getRemoteData(_file);
	QString qdata = QString::fromStdString(data);
	return MatchFileContent(qdata);
}

bool MacroConditionFile::CheckLocalFileContent()
{
	QString t = QString::fromStdString(_text);
	QFile file(QString::fromStdString(_file));
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		return false;
	}

	if (_useTime) {
		QDateTime newLastMod = QFileInfo(file).lastModified();
		if (_lastMod == newLastMod) {
			return false;
		}
		_lastMod = newLastMod;
	}

	QString filedata = QTextStream(&file).readAll();
	bool match = MatchFileContent(filedata);

	file.close();
	return match;
}

bool MacroConditionFile::CheckCondition()
{
	if (_fileType == FileType::REMOTE) {
		return CheckRemoteFileContent();
	} else {
		return CheckLocalFileContent();
	}
}

bool MacroConditionFile::Save(obs_data_t *obj)
{
	MacroCondition::Save(obj);
	_regex.Save(obj);
	obs_data_set_string(obj, "file", _file.c_str());
	obs_data_set_string(obj, "text", _text.c_str());
	obs_data_set_int(obj, "fileType", static_cast<int>(_fileType));
	obs_data_set_bool(obj, "useTime", _useTime);
	obs_data_set_bool(obj, "onlyMatchIfChanged", _onlyMatchIfChanged);
	return true;
}

bool MacroConditionFile::Load(obs_data_t *obj)
{
	MacroCondition::Load(obj);
	_regex.Load(obj);
	// TODO: remove in future version
	if (obs_data_has_user_value(obj, "useRegex")) {
		_regex.CreateBackwardsCompatibleRegex(
			obs_data_get_bool(obj, "useRegex"));
	}
	_file = obs_data_get_string(obj, "file");
	_text = obs_data_get_string(obj, "text");
	_fileType = static_cast<FileType>(obs_data_get_int(obj, "fileType"));
	_useTime = obs_data_get_bool(obj, "useTime");
	_onlyMatchIfChanged = obs_data_get_bool(obj, "onlyMatchIfChanged");
	return true;
}

std::string MacroConditionFile::GetShortDesc()
{
	return _file;
}

MacroConditionFileEdit::MacroConditionFileEdit(
	QWidget *parent, std::shared_ptr<MacroConditionFile> entryData)
	: QWidget(parent)
{
	_fileType = new QComboBox();
	_filePath = new FileSelection();
	_matchText = new ResizingPlainTextEdit(this);
	_regex = new RegexConfigWidget(parent);
	_checkModificationDate = new QCheckBox(obs_module_text(
		"AdvSceneSwitcher.fileTab.checkfileContentTime"));
	_checkFileContent = new QCheckBox(
		obs_module_text("AdvSceneSwitcher.fileTab.checkfileContent"));

	QWidget::connect(_fileType, SIGNAL(currentIndexChanged(int)), this,
			 SLOT(FileTypeChanged(int)));
	QWidget::connect(_filePath, SIGNAL(PathChanged(const QString &)), this,
			 SLOT(PathChanged(const QString &)));
	QWidget::connect(_matchText, SIGNAL(textChanged()), this,
			 SLOT(MatchTextChanged()));
	QWidget::connect(_regex, SIGNAL(RegexConfigChanged(RegexConfig)), this,
			 SLOT(RegexChanged(RegexConfig)));
	QWidget::connect(_checkModificationDate, SIGNAL(stateChanged(int)),
			 this, SLOT(CheckModificationDateChanged(int)));
	QWidget::connect(_checkFileContent, SIGNAL(stateChanged(int)), this,
			 SLOT(OnlyMatchIfChangedChanged(int)));

	_fileType->addItem(obs_module_text("AdvSceneSwitcher.fileTab.local"));
	_fileType->addItem(obs_module_text("AdvSceneSwitcher.fileTab.remote"));

	std::unordered_map<std::string, QWidget *> widgetPlaceholders = {
		{"{{fileType}}", _fileType},
		{"{{filePath}}", _filePath},
		{"{{matchText}}", _matchText},
		{"{{useRegex}}", _regex},
		{"{{checkModificationDate}}", _checkModificationDate},
		{"{{checkFileContent}}", _checkFileContent},
	};

	QVBoxLayout *mainLayout = new QVBoxLayout;
	QHBoxLayout *line1Layout = new QHBoxLayout;
	QHBoxLayout *line2Layout = new QHBoxLayout;
	QHBoxLayout *line3Layout = new QHBoxLayout;
	placeWidgets(
		obs_module_text("AdvSceneSwitcher.condition.file.entry.line1"),
		line1Layout, widgetPlaceholders);
	placeWidgets(
		obs_module_text("AdvSceneSwitcher.condition.file.entry.line2"),
		line2Layout, widgetPlaceholders, false);
	placeWidgets(
		obs_module_text("AdvSceneSwitcher.condition.file.entry.line3"),
		line3Layout, widgetPlaceholders);
	mainLayout->addLayout(line1Layout);
	mainLayout->addLayout(line2Layout);
	mainLayout->addLayout(line3Layout);

	setLayout(mainLayout);

	_entryData = entryData;
	UpdateEntryData();
	_loading = false;
}

void MacroConditionFileEdit::UpdateEntryData()
{
	if (!_entryData) {
		return;
	}

	_fileType->setCurrentIndex(static_cast<int>(_entryData->_fileType));

	_filePath->SetPath(QString::fromStdString(_entryData->_file));
	_matchText->setPlainText(QString::fromStdString(_entryData->_text));
	_regex->SetRegexConfig(_entryData->_regex);
	_checkModificationDate->setChecked(_entryData->_useTime);
	_checkFileContent->setChecked(_entryData->_onlyMatchIfChanged);

	adjustSize();
	updateGeometry();
}

void MacroConditionFileEdit::FileTypeChanged(int index)
{
	if (_loading || !_entryData) {
		return;
	}

	MacroConditionFile::FileType type =
		static_cast<MacroConditionFile::FileType>(index);

	if (type == MacroConditionFile::FileType::LOCAL) {
		_filePath->Button()->setDisabled(false);
		_checkModificationDate->setDisabled(false);
	} else {
		_filePath->Button()->setDisabled(true);
		_checkModificationDate->setDisabled(true);
	}

	std::lock_guard<std::mutex> lock(switcher->m);
	_entryData->_fileType = type;
}

void MacroConditionFileEdit::PathChanged(const QString &text)
{
	if (_loading || !_entryData) {
		return;
	}

	std::lock_guard<std::mutex> lock(switcher->m);
	_entryData->_file = text.toUtf8().constData();
	emit HeaderInfoChanged(
		QString::fromStdString(_entryData->GetShortDesc()));
}

void MacroConditionFileEdit::MatchTextChanged()
{
	if (_loading || !_entryData) {
		return;
	}

	std::lock_guard<std::mutex> lock(switcher->m);
	_entryData->_text = _matchText->toPlainText().toUtf8().constData();

	adjustSize();
	updateGeometry();
}

void MacroConditionFileEdit::RegexChanged(RegexConfig conf)
{
	if (_loading || !_entryData) {
		return;
	}

	std::lock_guard<std::mutex> lock(switcher->m);
	_entryData->_regex = conf;
	adjustSize();
	updateGeometry();
}

void MacroConditionFileEdit::CheckModificationDateChanged(int state)
{
	if (_loading || !_entryData) {
		return;
	}

	std::lock_guard<std::mutex> lock(switcher->m);
	_entryData->_useTime = state;
}

void MacroConditionFileEdit::OnlyMatchIfChangedChanged(int state)
{
	if (_loading || !_entryData) {
		return;
	}

	std::lock_guard<std::mutex> lock(switcher->m);
	_entryData->_onlyMatchIfChanged = state;
}
