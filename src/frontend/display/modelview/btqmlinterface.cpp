/*********
*
* In the name of the Father, and of the Son, and of the Holy Spirit.
*
* This file is part of BibleTime's source code, https://bibletime.info/
*
* Copyright 1999-2021 by the BibleTime developers.
* The BibleTime source code is licensed under the GNU General Public License
* version 2.0.
*
**********/

#include "btqmlinterface.h"

#include <QApplication>
#include <QClipboard>
#include <QScreen>
#include <QRegularExpression>
#include <QTimerEvent>
#include <utility>
#include "../../../backend/config/btconfig.h"
#include "../../../backend/drivers/cswordbookmoduleinfo.h"
#include "../../../backend/drivers/cswordlexiconmoduleinfo.h"
#include "../../../backend/drivers/cswordmoduleinfo.h"
#include "../../../backend/keys/cswordkey.h"
#include "../../../backend/managers/colormanager.h"
#include "../../../backend/managers/cswordbackend.h"
#include "../../../backend/models/btmoduletextmodel.h"
#include "../../../backend/rendering/btinforendering.h"
#include "../../../backend/rendering/cplaintextexportrendering.h"
#include "../../../backend/rendering/ctextrendering.h"
#include "../../../backend/rendering/btinforendering.h"
#include "../../../util/btassert.h"
#include "../../bibletime.h"
#include "../../cinfodisplay.h"
#include "../../edittextwizard/btedittextwizard.h"


BtQmlInterface::BtQmlInterface(QObject * parent)
    : QObject(parent)
    , m_moduleTextModel(new BtModuleTextModel(this))
{ m_moduleTextModel->setTextFilter(&m_textFilter); }

BtQmlInterface::~BtQmlInterface() = default;

bool BtQmlInterface::isBibleOrCommentary() {
    if (!m_swordKey)
        return false;
    switch (m_swordKey->module()->type()) {
    case CSwordModuleInfo::Bible: [[fallthrough]];
    case CSwordModuleInfo::Commentary: return true;
    default: return false;
    }
}

void BtQmlInterface::setFilterOptions(FilterOptions filterOptions) {
    m_moduleTextModel->setFilterOptions(filterOptions);
}

int BtQmlInterface::getContextMenuIndex() const {
    return m_contextMenuIndex;
}

void BtQmlInterface::setContextMenuIndex(int index) {
    m_contextMenuIndex = index;
    Q_EMIT contextMenuIndexChanged();
}

int BtQmlInterface::getContextMenuColumn() const {
    return m_contextMenuColumn;
}

void BtQmlInterface::setContextMenuColumn(int index) {
    m_contextMenuColumn = index;
    Q_EMIT contextMenuColumnChanged();
}

QColor BtQmlInterface::getBackgroundColor() const
{ return ColorManager::getBackgroundColor(); }

QColor BtQmlInterface::getBackgroundHighlightColor() const
{ return ColorManager::getBackgroundHighlightColor(); }

int BtQmlInterface::getBackgroundHighlightColorIndex() const {
    return m_backgroundHighlightColorIndex;
}

QColor BtQmlInterface::getForegroundColor() const
{ return ColorManager::getForegroundColor(); }

int BtQmlInterface::getCurrentModelIndex() const {
    if (m_swordKey == nullptr)
        return 0;
    auto const * const keyModule = m_swordKey->module();
    auto const moduleType = keyModule->type();
    if (moduleType == CSwordModuleInfo::Bible
        || moduleType == CSwordModuleInfo::Commentary)
    {
        return m_moduleTextModel->verseKeyToIndex(
                    *static_cast<CSwordVerseKey *>(m_swordKey));
    } else if (moduleType == CSwordModuleInfo::GenericBook) {
        auto const m = static_cast<CSwordBookModuleInfo const *>(keyModule);
        CSwordTreeKey key(m->tree(), m);
        key.setKey(m_swordKey->key());
        CSwordTreeKey p(key);
        p.positionToRoot();
        if(p != key)
            return static_cast<int>(key.offset() / 4u); /// \todo Check range!
    } else if (moduleType == CSwordModuleInfo::Lexicon) {
        return static_cast<CSwordLexiconModuleInfo const *>(
                    keyModule)->entries().indexOf(m_swordKey->key());
    }
    return 0;
}

int BtQmlInterface::getNumModules() const {
    return m_moduleNames.count();
}

double BtQmlInterface::getPixelsPerMM() const {
    constexpr static double const millimetersPerInch = 25.4;
    return QGuiApplication::screens().first()->physicalDotsPerInchX()
            / millimetersPerInch;
}

void BtQmlInterface::setSelection(int const column,
                                  int const startIndex,
                                  int const endIndex,
                                  QString const & selectedText)
{
    BT_ASSERT(column >= 0);
    BT_ASSERT(startIndex >= 0);
    BT_ASSERT(endIndex >= 0);
    BT_ASSERT(!selectedText.isEmpty());
    m_selection.emplace(Selection{column, startIndex, endIndex, selectedText});
}

void BtQmlInterface::clearSelection() noexcept { m_selection.reset(); }

bool BtQmlInterface::hasSelectedText() const noexcept
{ return m_selection.has_value(); }

QString BtQmlInterface::getSelectedText() const
{ return m_selection->selectedText; }

QString BtQmlInterface::getRawText(int row, int column) {
    BT_ASSERT(column >= 0 && column <= m_moduleNames.count());
    CSwordVerseKey key = m_moduleTextModel->indexToVerseKey(row);
    QString moduleName = m_moduleNames.at(column);
    auto * const module =
            CSwordBackend::instance().findModuleByName(moduleName);
    CSwordVerseKey mKey(module);
    mKey.setKey(key.key());
    auto rawText = mKey.rawText();

    /* Since rawText is a complete HTML page at the moment, strip away headers
       and footers of a HTML page, keeping only the contents of <body>: */
    static auto const reFlags =
            QRegularExpression::CaseInsensitiveOption
            | QRegularExpression::DotMatchesEverythingOption
            | QRegularExpression::DontCaptureOption
            | QRegularExpression::UseUnicodePropertiesOption;
    static QRegularExpression const reBefore(
                QStringLiteral("^.*?<body(\\s[^>]*?)?>"), reFlags);
    static QRegularExpression const reAfter(
                QStringLiteral("</body>.*?$"), reFlags);
    if (auto const m = reBefore.match(rawText); m.hasMatch())
        rawText.remove(0, m.capturedLength());
    if (auto const m = reAfter.match(rawText); m.hasMatch())
        rawText.chop(m.capturedLength());
    return rawText;
}

void BtQmlInterface::openEditor(int row, int column) {
    BtEditTextWizard wiz;
    wiz.setTitle(tr("Edit %1").arg(m_moduleTextModel->indexToKeyName(row)));
    wiz.setText(getRawText(row, column));
    wiz.setFont(m_fonts.at(column));
    if (wiz.exec() == QDialog::Accepted)
        setRawText(row, column, wiz.text());
}

int BtQmlInterface::indexToVerse(int index) {
    return m_moduleTextModel->indexToVerse(index);
}

void BtQmlInterface::setHoveredLink(QString const & link) {
    if (QGuiApplication::keyboardModifiers() & Qt::ShiftModifier)
        return;
    setMagReferenceByUrl(link);
    m_activeLink = link;
}

QString BtQmlInterface::getLemmaFromLink(const QString& url) {
    QString reference;

    QRegExp rx(QStringLiteral("sword://lemmamorph/([a-s]+)=([GH][0-9]+)"));
    rx.setMinimal(false);
    int pos1 = rx.indexIn(url);
    if (pos1 > -1) {
        reference = rx.cap(2);
    }
    return reference;
}

QString BtQmlInterface::getBibleUrlFromLink(const QString& url) {
    QString reference;

    QRegExp rx(QStringLiteral("(sword://Bible/.*)\\|\\|(.*)=(.*)"));
    rx.setMinimal(false);
    int pos1 = rx.indexIn(url);
    if (pos1 > -1) {
        reference = rx.cap(1);
    }
    return reference;
}

QString BtQmlInterface::getReferenceFromUrl(const QString& url) {
    {
        QRegExp rx(QStringLiteral("sword://(bible|lexicon)/(.*)/(.*)(\\|\\|)"),
                   Qt::CaseInsensitive);
        rx.setMinimal(false);
        if (rx.indexIn(url) > -1)
            return QStringLiteral("href=sword://%1/%2/%3")
                                  .arg(rx.cap(1), rx.cap(2), rx.cap(3));
    }{
        QRegExp rx(QStringLiteral("sword://(bible|lexicon)/(.*)/(.*)"),
                   Qt::CaseInsensitive);
        rx.setMinimal(false);
        if (rx.indexIn(url) > -1)
            return QStringLiteral("href=sword://%1/%2/%3")
                    .arg(rx.cap(1), rx.cap(2), rx.cap(3));
    }{
        QRegExp rx(QStringLiteral("sword://footnote/(.*)=(.*)"),
                   Qt::CaseInsensitive);
        rx.setMinimal(false);
        if (rx.indexIn(url) > -1)
            return QStringLiteral("note=") + rx.cap(1);
    }{
        QRegExp rx(QStringLiteral("sword://lemmamorph/(.*)=(.*)/(.*)"),
                   Qt::CaseInsensitive);
        rx.setMinimal(false);
        if (rx.indexIn(url) > -1)
            return QStringLiteral("%1=%2").arg(rx.cap(1), rx.cap(2));
    }
    return {};
}

QString BtQmlInterface::rawText(int const row, int const column) {
    return m_moduleTextModel->data(m_moduleTextModel->index(row, 0),
                                   ModuleEntry::Text0Role + column).toString();
}

void BtQmlInterface::setRawText(int row, int column, const QString& text) {
    QModelIndex index = m_moduleTextModel->index(row, 0);
    int role = ModuleEntry::Text0Role + column;
    m_moduleTextModel->setData(index, text, role);
}

void BtQmlInterface::cancelMagTimer() {
    killTimer(m_linkTimerId);
    m_linkTimerId = 0;
}

void BtQmlInterface::setMagReferenceByUrl(const QString& url) {
    if (url.isEmpty())
        return;
    m_timeoutUrl = url;
    cancelMagTimer();
    m_linkTimerId = startTimer(400);
}

void BtQmlInterface::settingsChanged() {
    getFontsFromSettings();
    changeColorTheme();
    Q_EMIT textChanged();
}

void BtQmlInterface::getFontsFromSettings() {
    decltype(m_fonts) newFonts;
    newFonts.reserve(m_moduleNames.size());
    for (auto const & moduleName : m_moduleNames) {
        if (auto const * const m =
                    CSwordBackend::instance().findModuleByName(moduleName))
        {
            if (auto const lang = m->language()) {
                BtConfig::FontSettingsPair fontPair = btConfig().getFontForLanguage(*lang);
                if (fontPair.first) {
                    newFonts.append(fontPair.second);
                    continue;
                }
            }
        }
        newFonts.append(btConfig().getDefaultFont());
    }
    m_fonts = std::move(newFonts);
    Q_EMIT fontChanged();
}

void BtQmlInterface::setBibleKey(const QString& link) {
    QRegExp rx(QStringLiteral("sword://Bible/(.*)/(.*)\\|\\|(.*)=(.*)"));
    rx.setMinimal(false);
    int pos1 = rx.indexIn(link);
    QString keyName;
    if (pos1 > -1) {
        keyName = rx.cap(2);
        Q_EMIT setBibleReference(keyName);
    }
}

void BtQmlInterface::scrollToSwordKey(CSwordKey * key) {
    m_backgroundHighlightColorIndex = m_moduleTextModel->keyToIndex(*key);

    /* Convert from sword index to ListView index */
    m_backgroundHighlightColorIndex = m_backgroundHighlightColorIndex - m_moduleTextModel->getFirstEntryIndex();

    Q_EMIT backgroundHighlightColorIndexChanged();
    m_swordKey = key;
    Q_EMIT currentModelIndexChanged();
}

void BtQmlInterface::setModules(const QStringList &modules) {
    m_moduleNames = modules;
    m_moduleTextModel->setModules(modules);
    getFontsFromSettings();
    Q_EMIT numModulesChanged();
}

void BtQmlInterface::changeReference(int i) {
    QString reference = m_moduleTextModel->indexToKeyName(i);
    Q_EMIT updateReference(reference);
}

void BtQmlInterface::dragHandler(int index) {
    QString moduleName;
    QString keyName;

    QRegExp rx(QStringLiteral("sword://Bible/(.*)/(.*)\\|\\|(.*)=(.*)"));
    rx.setMinimal(false);
    int pos1 = rx.indexIn(m_activeLink);

    if (pos1 > -1) {
        moduleName = rx.cap(1);
        keyName = rx.cap(2);
    } else {
        moduleName = m_moduleNames.at(0);
        keyName = m_moduleTextModel->indexToKeyName(index);
    }

    Q_EMIT dragOccuring(moduleName, keyName);
}

CSwordKey* BtQmlInterface::getMouseClickedKey() const {
    return m_moduleTextModel->indexToKey(m_contextMenuIndex, m_contextMenuColumn);
}

QFont BtQmlInterface::font(int column) const {
    if (column >= 0 && column < m_fonts.count())
        return m_fonts.at(column);
    return QApplication::font();
}

QFont BtQmlInterface::getFont0() const { return font(0); }
QFont BtQmlInterface::getFont1() const { return font(1); }
QFont BtQmlInterface::getFont2() const { return font(2); }
QFont BtQmlInterface::getFont3() const { return font(3); }
QFont BtQmlInterface::getFont4() const { return font(4); }
QFont BtQmlInterface::getFont5() const { return font(5); }
QFont BtQmlInterface::getFont6() const { return font(6); }
QFont BtQmlInterface::getFont7() const { return font(7); }
QFont BtQmlInterface::getFont8() const { return font(8); }
QFont BtQmlInterface::getFont9() const { return font(9); }

QVariant BtQmlInterface::getTextModel() {
    QVariant var;
    var.setValue(m_moduleTextModel);
    return var;
}

BtModuleTextModel * BtQmlInterface::textModel() {
    return m_moduleTextModel;
}

BtModuleTextModel const * BtQmlInterface::textModel() const
{ return m_moduleTextModel; }

bool BtQmlInterface::moduleIsWritable(int column) {
    if (column >= m_moduleNames.count())
        return false;
    QString moduleName = m_moduleNames.at(column);
    auto * const module =
            CSwordBackend::instance().findModuleByName(moduleName);
    BT_ASSERT(module);
    return module->isWritable();
}

void BtQmlInterface::changeColorTheme() {
    Q_EMIT backgroundHighlightColorChanged();
    Q_EMIT backgroundColorChanged();
    Q_EMIT foregroundColorChanged();
}

void BtQmlInterface::copyRange(int index1, int index2) const {
    QString text;
    std::unique_ptr<CSwordKey> key(m_swordKey->copy());

    for (int i=index1; i<=index2; ++i) {
        QString keyName = m_moduleTextModel->indexToKeyName(i);
        key->setKey(keyName);
        text.append(QStringLiteral("%1\n%2\n\n")
                    .arg(keyName, key->strippedText()));
    }
    QClipboard *clipboard = QGuiApplication::clipboard();
    clipboard->setText(text);
}

void BtQmlInterface::copyVerseRange(CSwordVerseKey const & key1,
                                    CSwordVerseKey const & key2)
{
    BT_ASSERT(key1.module());
    BT_ASSERT(key1.module() == key2.module());

    Rendering::CPlainTextExportRendering render(true);
    {
        DisplayOptions displayOptions;
        displayOptions.lineBreaks = true;
        displayOptions.verseNumbers = true;
        render.setDisplayOptions(displayOptions);
    }{
        FilterOptions filterOptions;
        filterOptions.footnotes = 0;
        filterOptions.greekAccents = 1;
        filterOptions.headings = 1;
        filterOptions.hebrewCantillation = 1;
        filterOptions.hebrewPoints = 1;
        filterOptions.lemmas = 0;
        filterOptions.morphSegmentation = 1;
        filterOptions.morphTags = 0;
        filterOptions.redLetterWords = 1;
        filterOptions.scriptureReferences = 0;
        filterOptions.strongNumbers = 0;
        filterOptions.textualVariants = 0;
        render.setFilterOptions(filterOptions);
    }
    QGuiApplication::clipboard()->setText(
                render.renderKeyRange(key1, key2, {key1.module()}));
}

void BtQmlInterface::setHighlightWords(const QString& words, bool caseSensitive) {
    m_throttledHighlightWords.emplace(
                ThrottledHighlightWords{words, caseSensitive});
    if (!m_highlightWordsTimerId)
        m_highlightWordsTimerId = startTimer(900);
}

void BtQmlInterface::timerEvent(QTimerEvent * const event) {
    auto const timerId = event->timerId();
    BT_ASSERT(timerId);
    if (timerId == m_linkTimerId) {
        event->accept();
        cancelMagTimer();
        auto infoList(Rendering::detectInfo(getReferenceFromUrl(m_timeoutUrl)));
        if (!infoList.isEmpty())
            BibleTime::instance()->infoDisplay()->setInfo(std::move(infoList));
    } else if (timerId == m_highlightWordsTimerId) {
        event->accept();
        if (m_throttledHighlightWords.has_value()) {
            if (m_throttledHighlightWords == m_lastAppliedHighlightWords) {
                // Value was changed and changed back. Reset and wait once more:
                m_throttledHighlightWords.reset();
            } else {
                QApplication::setOverrideCursor(Qt::WaitCursor);
                m_moduleTextModel->setHighlightWords(
                            m_throttledHighlightWords->words,
                            m_throttledHighlightWords->caseSensitive);
                m_findState.reset();
                m_moduleTextModel->setFindState(m_findState);
                m_lastAppliedHighlightWords =
                        std::move(m_throttledHighlightWords);
                m_throttledHighlightWords.reset(); // contains moved-from value
                QApplication::restoreOverrideCursor();
            }
        } else {
            killTimer(m_highlightWordsTimerId);
            m_highlightWordsTimerId = 0;
        }
    } else {
        QObject::timerEvent(event);
    }
}

void BtQmlInterface::findText(bool const backward) {
    QApplication::setOverrideCursor(Qt::WaitCursor);
    if (!m_findState)
        m_findState = FindState{getCurrentModelIndex(), 0};

    if (backward)
        getPreviousMatchingItem(m_findState->index);
    else
        getNextMatchingItem(m_findState->index);

    m_moduleTextModel->setFindState(m_findState);
    Q_EMIT positionItemOnScreen(m_findState->index);
    QApplication::restoreOverrideCursor();
}

int BtQmlInterface::countHighlightsInItem(int index) {
    QModelIndex mIndex = m_moduleTextModel->index(index);
    QString text = m_moduleTextModel->data(mIndex, ModuleEntry::Text1Role).toString();
    int num = text.count(QStringLiteral("\"highlightwords"));
    return num;
}

void BtQmlInterface::getNextMatchingItem(int startIndex) {
    int num = countHighlightsInItem(startIndex);
    if (num > m_findState->subIndex) { // Found within startIndex item
        m_findState->index = startIndex;
        ++m_findState->subIndex;
        return;
    }

    if (startIndex >= m_moduleTextModel->rowCount())
        return;

    int index = startIndex+1;
    for (int i = 0; i < 1000; ++i) {
        int num = countHighlightsInItem(index);
        if (num > 0 ) {
            m_findState->index = index;
            m_findState->subIndex = 1;
            return;
        }
        ++index;
    }
    return;
}

void BtQmlInterface::getPreviousMatchingItem(int startIndex) {
    int num = countHighlightsInItem(startIndex);
    if (num > 0 && m_findState->subIndex == 0) {
        // Found within startIndex item
        m_findState->index = startIndex;
        m_findState->subIndex = 1;
        return;
    }

    if (startIndex <= 0)
        return;

    int index = startIndex;
    if (m_findState->subIndex == 0)
        --index;
    for (int i = 0; i < 1000; ++i) {
        int num = countHighlightsInItem(index);
        if (num > 0 ) {
            m_findState->index = index;
            if (m_findState->subIndex == 0)
                m_findState->subIndex = num;
            else
                --m_findState->subIndex;
            if (m_findState->subIndex != 0)
                return;
        }
        --index;
    }
    return;
}
