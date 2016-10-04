/*!
 * @file arpwidget.cpp
 * @brief Implements the ArpWidget GUI class.
 *
 * @section LICENSE
 *
 *      Copyright 2009 - 2016 <qmidiarp-devel@lists.sourceforge.net>
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *      MA 02110-1301, USA.
 */

#include <QBoxLayout>
#include <QFile>
#include <QTextStream>
#include <QInputDialog>
#include <QDir>
#include <QMessageBox>

#include "midiarp.h"
#include "arpwidget.h"
#include "slider.h"
#include "arpscreen.h"
#include "config.h"

#include "pixmaps/editmodeon.xpm"
#include "pixmaps/latchmodeon.xpm"
#ifdef APPBUILD
#include "pixmaps/patternremove.xpm"
#include "pixmaps/patternstore.xpm"
#endif



#ifdef APPBUILD
ArpWidget::ArpWidget(MidiArp *p_midiWorker, GlobStore *p_globStore,
    int portCount, bool compactStyle,
    bool mutedAdd, bool inOutVisible, const QString& name):
    InOutBox(portCount, compactStyle, inOutVisible, "Arp:"),
    midiWorker(p_midiWorker),
    globStore(p_globStore),
    modified(false)
{
    midiControl = new MidiControl(this);
#else
ArpWidget::ArpWidget(
    bool compactStyle,
    bool mutedAdd, bool inOutVisible):
    InOutBox(compactStyle, inOutVisible, "Arp:"),
    midiWorker(NULL),
    modified(false)
{
#endif
    connect(enableRestartByKbd, SIGNAL(toggled(bool)), this, 
            SLOT(updateEnableRestartByKbd(bool)));
    connect(enableTrigByKbd, SIGNAL(toggled(bool)), this, 
            SLOT(updateEnableTrigByKbd(bool)));
    connect(enableTrigLegato, SIGNAL(toggled(bool)), this, 
            SLOT(updateTrigLegato(bool)));
    connect(chIn, SIGNAL(activated(int)), this, 
            SLOT(updateChIn(int)));
    connect(indexIn[0], SIGNAL(valueChanged(int)), this,
            SLOT(updateIndexIn(int)));
    connect(indexIn[1], SIGNAL(valueChanged(int)), this,
            SLOT(updateIndexIn(int)));
    connect(rangeIn[0], SIGNAL(valueChanged(int)), this,
            SLOT(updateRangeIn(int)));
    connect(rangeIn[1], SIGNAL(valueChanged(int)), this,
            SLOT(updateRangeIn(int)));
    connect(channelOut, SIGNAL(activated(int)), this,
            SLOT(updateChannelOut(int)));
#ifdef APPBUILD
    connect(portOut, SIGNAL(activated(int)), this, 
            SLOT(updatePortOut(int)));
    midiControl->addMidiLearnMenu("Note Low", indexIn[0], 10);
    midiControl->addMidiLearnMenu("Note Hi", indexIn[1], 11);
#endif

    hideInOutBoxAction = new QAction(tr("&Show/hide in-out settings"), this);
    QToolButton *hideInOutBoxButton = new QToolButton;
    hideInOutBoxAction->setCheckable(true);
    hideInOutBoxAction->setChecked(inOutVisible);
    hideInOutBoxButton->setDefaultAction(hideInOutBoxAction);
    hideInOutBoxButton->setFixedSize(10, 80);
    hideInOutBoxButton->setArrowType (Qt::ArrowType(0));
    connect(hideInOutBoxAction, SIGNAL(toggled(bool)), inOutBoxWidget, SLOT(setVisible(bool)));


    // group box for pattern setup
    QGroupBox *patternBox = new QGroupBox(tr("Pattern"));
    QVBoxLayout *patternBoxLayout = new QVBoxLayout;

    textEditButton = new QToolButton;
    textEditAction = new QAction(QPixmap(editmodeon_xpm),
            tr("&Edit Pattern"), this);
    connect(textEditAction, SIGNAL(toggled(bool)), this,
            SLOT(openTextEditWindow(bool)));
    textEditAction->setCheckable(true);
    textEditButton->setDefaultAction(textEditAction);

#ifdef APPBUILD
    textRemoveButton = new QToolButton;
    textRemoveAction = new QAction(QPixmap(patternremove_xpm),
            tr("&Remove Pattern"), this);
    connect(textRemoveAction, SIGNAL(triggered()), this,
            SLOT(removeCurrentPattern()));
    textRemoveButton->setDefaultAction(textRemoveAction);
    textRemoveAction->setEnabled(false);

    textStoreButton = new QToolButton;
    textStoreAction = new QAction(QPixmap(patternstore_xpm),
            tr("&Store Pattern"), this);
    connect(textStoreAction, SIGNAL(triggered()), this,
            SLOT(storeCurrentPattern()));
    textStoreAction->setEnabled(false);
    textStoreButton->setDefaultAction(textStoreAction);
#endif

    patternPresetBox = new QComboBox;
    loadPatternPresets();
    patternPresetBox->insertItems(0, patternNames);
    patternPresetBox->setCurrentIndex(0);
    patternPresetBoxIndex = 0;
    patternPresetBox->setToolTip(tr("Pattern preset"));
    patternPresetBox->setMinimumContentsLength(20);
    connect(patternPresetBox, SIGNAL(activated(int)), this,
            SLOT(selectPatternPreset(int)));
#ifdef APPBUILD
    midiControl->addMidiLearnMenu("PresetSwitch", patternPresetBox, 1);
#endif
    muteOutAction = new QAction(tr("&Mute"),this);
    muteOutAction->setCheckable(true);
    connect(muteOutAction, SIGNAL(toggled(bool)), this, SLOT(setMuted(bool)));
    muteOut = new QToolButton;
    muteOut->setDefaultAction(muteOutAction);
    muteOut->setFont(QFont("Helvetica", 8));
    muteOut->setMinimumSize(QSize(35,20));
#ifdef APPBUILD
    midiControl->addMidiLearnMenu("MuteToggle", muteOut, 0);
#endif
    deferChangesAction = new QAction("D", this);
    deferChangesAction->setToolTip(tr("Defer mute to pattern end"));
    deferChangesAction->setCheckable(true);
    connect(deferChangesAction, SIGNAL(toggled(bool)), this, SLOT(updateDeferChanges(bool)));

    QToolButton *deferChangesButton = new QToolButton;
    deferChangesButton->setDefaultAction(deferChangesAction);
    deferChangesButton->setFixedSize(20, 20);

    repeatPatternThroughChord = new QComboBox;
    QStringList repeatPatternNames;
    repeatPatternNames << tr("Static") << tr("Up") << tr("Down") << tr("Random");
    repeatPatternThroughChord->insertItems(0, repeatPatternNames);
    repeatPatternThroughChord->setToolTip(tr("Repeat mode - This is how notes are sequenced\n"
                            "when a chord is pressed"));
    connect(repeatPatternThroughChord, SIGNAL(currentIndexChanged(int)), this,
            SLOT(updateRepeatPattern(int)));
    repeatPatternThroughChord->setCurrentIndex(1);

    octaveModeBox = new QComboBox;
    repeatPatternNames.clear();
    repeatPatternNames << tr("Static") << tr("Up") << tr("Down") << tr("Bounce");
    octaveModeBox->insertItems(0, repeatPatternNames);
    octaveModeBox->setToolTip(tr("Octave mode - The overall octave changes like this\n"
                            "once all pressed notes were played through"));
    connect(octaveModeBox, SIGNAL(currentIndexChanged(int)), this,
            SLOT(updateOctaveMode(int)));
    octaveModeBox->setCurrentIndex(0);

    octaveLowBox = new QComboBox;
    repeatPatternNames.clear();
    repeatPatternNames << "0" << "-1" << "-2" << "-3";
    octaveLowBox->insertItems(0, repeatPatternNames);
    octaveLowBox->setToolTip(tr("Low octave limit"));
    connect(octaveLowBox, SIGNAL(currentIndexChanged(int)), this,
            SLOT(updateOctaveLow(int)));
    octaveLowBox->setCurrentIndex(0);

    octaveHighBox = new QComboBox;
    repeatPatternNames.clear();
    repeatPatternNames << "0" << "1" << "2" << "3";
    octaveHighBox->insertItems(0, repeatPatternNames);
    octaveHighBox->setToolTip(tr("High octave limit"));
    connect(octaveHighBox, SIGNAL(currentIndexChanged(int)), this,
            SLOT(updateOctaveHigh(int)));
    octaveHighBox->setCurrentIndex(0);

    latchModeButton = new QToolButton;
    latchModeAction = new QAction(QPixmap(latchmodeon_xpm),
            tr("&Latch Mode"), this);
    connect(latchModeAction, SIGNAL(toggled(bool)), this,
            SLOT(setLatchMode(bool)));
    latchModeAction->setCheckable(true);
    latchModeButton->setDefaultAction(latchModeAction);

    QHBoxLayout *patternPresetLayout = new QHBoxLayout;
    if (compactStyle) {
        patternPresetLayout->setMargin(2);
        patternPresetLayout->setSpacing(1);
    }
    patternPresetLayout->addWidget(patternPresetBox);
    patternPresetLayout->addWidget(textEditButton);
#ifdef APPBUILD
    patternPresetLayout->addWidget(textStoreButton);
    patternPresetLayout->addWidget(textRemoveButton);
#endif
    patternPresetLayout->addStretch(2);

    QHBoxLayout *modeLayout = new QHBoxLayout;
    if (compactStyle) {
        modeLayout->setMargin(2);
        modeLayout->setSpacing(1);
    }

    modeLayout->addWidget(muteOut);
    modeLayout->addWidget(deferChangesButton);
    modeLayout->addWidget(repeatPatternThroughChord);
    modeLayout->addWidget(octaveModeBox);
    modeLayout->addWidget(octaveLowBox);
    modeLayout->addWidget(octaveHighBox);
    modeLayout->addWidget(latchModeButton);
    modeLayout->addStretch(2);

    patternText = new QLineEdit;
    connect(patternText, SIGNAL(textChanged(const QString&)), this,
            SLOT(updateText(const QString&)));
    patternText->setHidden(true);
    patternText->setToolTip(
            tr("0..9  note played on keyboard, 0 is lowest\n"
            "( ) numbers in parenthesis are stacked to chords\n"
            "  + = -  octave up/reset/down\n"
            "  t = g  semitone up/reset/down\n"
            " < . > tempo up/reset/down\n"
            "  d h  note length up/down\n"
            "  / \\  velocity up/down\n"
            "   p   pause"));


    QWidget *screenBox = new QWidget;
    QHBoxLayout *screenBoxLayout = new QHBoxLayout;
    screen = new ArpScreen(this);
    screenBox->setMinimumHeight(80);
    screenBoxLayout->addWidget(screen);
    screenBoxLayout->setMargin(2);
    screenBoxLayout->setSpacing(1);
    screenBox->setLayout(screenBoxLayout);

    patternBoxLayout->addWidget(screenBox);
    patternBoxLayout->addLayout(patternPresetLayout);
    patternBoxLayout->addWidget(patternText);
    patternBoxLayout->addLayout(modeLayout);
    if (compactStyle) {
        patternBoxLayout->setMargin(2);
        patternBoxLayout->setSpacing(1);
    }
    patternBox->setLayout(patternBoxLayout);

    // group box for random settings
    randomBox = new QGroupBox(tr("Random"));
    QVBoxLayout *randomBoxLayout = new QVBoxLayout;

    randomTick = new Slider(0, 100, 1, 5, 0, Qt::Horizontal,
            tr("&Shift"), randomBox);
    connect(randomTick, SIGNAL(valueChanged(int)), this,
            SLOT(updateRandomTickAmp(int)));

    randomVelocity = new Slider(0, 100, 1, 5, 0, Qt::Horizontal,
            tr("Vel&ocity"), randomBox);
    connect(randomVelocity, SIGNAL(valueChanged(int)), this,
            SLOT(updateRandomVelocityAmp(int)));

    randomLength = new Slider(0, 100, 1, 5, 0, Qt::Horizontal,
            tr("&Length"), randomBox);
    connect(randomLength, SIGNAL(valueChanged(int)), this,
            SLOT(updateRandomLengthAmp(int)));

    randomBoxLayout->addWidget(randomTick);
    randomBoxLayout->addWidget(randomVelocity);
    randomBoxLayout->addWidget(randomLength);
    randomBoxLayout->addStretch();
    if (compactStyle) {
        randomBoxLayout->setSpacing(1);
        randomBoxLayout->setMargin(2);
    }
    randomBox->setCheckable(true);
    connect(randomBox, SIGNAL(toggled(bool)), this,
            SLOT(setRandomVisible(bool)));
    randomBox->setChecked(false);
    randomBox->setFlat(true);
    randomBox->setLayout(randomBoxLayout);

    envelopeBox = new QGroupBox(tr("Envelope"));
    QVBoxLayout *envelopeBoxLayout = new QVBoxLayout;
    attackTime = new Slider(0, 20, 1, 1, 0, Qt::Horizontal,
            tr("&Attack (beats)"), this);
    connect(attackTime, SIGNAL(valueChanged(int)), this,
            SLOT(updateAttackTime(int)));
    releaseTime = new Slider(0, 20, 1, 1, 0, Qt::Horizontal,
            tr("&Release (beats)"), this);
    connect(releaseTime, SIGNAL(valueChanged(int)), this,
            SLOT(updateReleaseTime(int)));

    envelopeBoxLayout->addWidget(attackTime);
    envelopeBoxLayout->addWidget(releaseTime);
    envelopeBoxLayout->addStretch();
    if (compactStyle) {
        envelopeBoxLayout->setSpacing(1);
        envelopeBoxLayout->setMargin(2);
    }
    envelopeBox->setCheckable(true);
    connect(envelopeBox, SIGNAL(toggled(bool)), this,
            SLOT(setEnvelopeVisible(bool)));
    envelopeBox->setChecked(false);
    envelopeBox->setFlat(true);
    envelopeBox->setLayout(envelopeBoxLayout);

#ifdef APPBUILD
        parStore = new ParStore(globStore, name, muteOutAction, deferChangesAction, this);
        midiControl->addMidiLearnMenu("Restore_"+name, parStore->topButton, 2);
        connect(parStore, SIGNAL(store(int, bool)),
                 this, SLOT(storeParams(int, bool)));
        connect(parStore, SIGNAL(restore(int)),
                 this, SLOT(restoreParams(int)));
#endif

    muteOutAction->setChecked(mutedAdd);

    QGridLayout *widgetLayout = new QGridLayout;
    widgetLayout->addWidget(patternBox, 0, 0);
    widgetLayout->addWidget(randomBox, 1, 0);
    widgetLayout->addWidget(envelopeBox, 2, 0);
    widgetLayout->addWidget(hideInOutBoxButton, 0, 1);
    widgetLayout->addWidget(inOutBoxWidget, 0, 2, 3, 1);
    widgetLayout->setRowStretch(3, 1);
    widgetLayout->setColumnStretch(0, 5);
    setLayout(widgetLayout);
    needsGUIUpdate=false;
}

ArpWidget::~ArpWidget()
{
#ifdef APPBUILD
    delete parStore;
#endif
}

#ifdef APPBUILD
MidiArp *ArpWidget::getMidiWorker()
{
    return (midiWorker);
}

void ArpWidget::writeData(QXmlStreamWriter& xml)
{
    xml.writeStartElement(manageBox->name.left(3));
    xml.writeAttribute("name", manageBox->name.mid(manageBox->name.indexOf(':') + 1));
    xml.writeAttribute("inOutVisible", QString::number(inOutBoxWidget->isVisible()));
        xml.writeStartElement("pattern");
            xml.writeTextElement("pattern", midiWorker->pattern);
            xml.writeTextElement("repeatMode", QString::number(
                midiWorker->repeatPatternThroughChord));
            xml.writeTextElement("octaveMode", QString::number(
                midiWorker->octMode));
            xml.writeTextElement("octaveLow", QString::number(
                midiWorker->octLow));
            xml.writeTextElement("octaveHigh", QString::number(
                midiWorker->octHigh));
            xml.writeTextElement("latchMode", QString::number(
                latchModeAction->isChecked()));
        xml.writeEndElement();

        xml.writeStartElement("input");
            xml.writeTextElement("channel", QString::number(
                midiWorker->chIn));
            xml.writeTextElement("indexMin", QString::number(
                midiWorker->indexIn[0]));
            xml.writeTextElement("indexMax", QString::number(
                midiWorker->indexIn[1]));
            xml.writeTextElement("rangeMin", QString::number(
                midiWorker->rangeIn[0]));
            xml.writeTextElement("rangeMax", QString::number(
                midiWorker->rangeIn[1]));
            xml.writeTextElement("restartByKbd", QString::number(
                midiWorker->restartByKbd));
            xml.writeTextElement("trigByKbd", QString::number(
                midiWorker->trigByKbd));
            xml.writeTextElement("trigLegato", QString::number(
                midiWorker->trigLegato));
        xml.writeEndElement();

        xml.writeStartElement("output");
            xml.writeTextElement("muted", QString::number(
                midiWorker->isMuted));
            xml.writeTextElement("defer", QString::number(
                midiWorker->deferChanges));
            xml.writeTextElement("port", QString::number(
                midiWorker->portOut));
            xml.writeTextElement("channel", QString::number(
                midiWorker->channelOut));
        xml.writeEndElement();

        xml.writeStartElement("random");
            xml.writeTextElement("tick", QString::number(
                midiWorker->randomTickAmp));
            xml.writeTextElement("velocity", QString::number(
                midiWorker->randomVelocityAmp));
            xml.writeTextElement("length", QString::number(
                midiWorker->randomLengthAmp));
        xml.writeEndElement();

        xml.writeStartElement("envelope");
            xml.writeTextElement("attack", QString::number(
                attackTime->value()));
            xml.writeTextElement("release", QString::number(
                releaseTime->value()));
        xml.writeEndElement();

        midiControl->writeData(xml);

        parStore->writeData(xml);

    xml.writeEndElement();
}

void ArpWidget::readData(QXmlStreamReader& xml)
{
    int tmp;

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isEndElement())
            break;

        if (xml.isStartElement() && (xml.name() == "pattern")) {
            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.isEndElement())
                    break;
                if (xml.name() == "pattern")
                    patternText->setText(xml.readElementText());
                else if (xml.name() == "repeatMode")
                    repeatPatternThroughChord->setCurrentIndex(xml.readElementText().toInt());
                else if (xml.name() == "octaveMode")
                    octaveModeBox->setCurrentIndex(xml.readElementText().toInt());
                else if (xml.name() == "octaveLow")
                    octaveLowBox->setCurrentIndex(-xml.readElementText().toInt());
                else if (xml.name() == "octaveHigh")
                    octaveHighBox->setCurrentIndex(xml.readElementText().toInt());
                else if (xml.name() == "latchMode")
                    latchModeAction->setChecked(xml.readElementText().toInt());
                else skipXmlElement(xml);
            }
        }

        else if (xml.isStartElement() && (xml.name() == "input")) {
            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.isEndElement())
                    break;
                if (xml.name() == "channel") {
                    tmp = xml.readElementText().toInt();
                    chIn->setCurrentIndex(tmp);
                    updateChIn(tmp);
                }
                else if (xml.name() == "indexMin")
                    indexIn[0]->setValue(xml.readElementText().toInt());
                else if (xml.name() == "indexMax")
                    indexIn[1]->setValue(xml.readElementText().toInt());
                else if (xml.name() == "rangeMin")
                    rangeIn[0]->setValue(xml.readElementText().toInt());
                else if (xml.name() == "rangeMax")
                    rangeIn[1]->setValue(xml.readElementText().toInt());
                else if (xml.name() == "restartByKbd")
                    enableRestartByKbd->setChecked(xml.readElementText().toInt());
                else if (xml.name() == "trigByKbd")
                    enableTrigByKbd->setChecked(xml.readElementText().toInt());
                else if (xml.name() == "trigLegato")
                    enableTrigLegato->setChecked(xml.readElementText().toInt());
                else skipXmlElement(xml);
            }
        }
        else if (xml.isStartElement() && (xml.name() == "output")) {
            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.isEndElement())
                    break;
                if (xml.name() == "muted")
                    muteOutAction->setChecked(xml.readElementText().toInt());
                else if (xml.name() == "defer")
                    deferChangesAction->setChecked(xml.readElementText().toInt());
                else if (xml.name() == "channel") {
                    tmp = xml.readElementText().toInt();
                    channelOut->setCurrentIndex(tmp);
                    updateChannelOut(tmp);
                }
                else if (xml.name() == "port") {
                    tmp = xml.readElementText().toInt();
                    portOut->setCurrentIndex(tmp);
                    updatePortOut(tmp);
                }
                else skipXmlElement(xml);
            }
        }
        else if (xml.isStartElement() && (xml.name() == "random")) {
            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.isEndElement())
                    break;
                if (xml.name() == "tick")
                    randomTick->setValue(xml.readElementText().toInt());
                else if (xml.name() == "velocity")
                    randomVelocity->setValue(xml.readElementText().toInt());
                else if (xml.name() == "length")
                    randomLength->setValue(xml.readElementText().toInt());
                else skipXmlElement(xml);
            }
        }
        else if (xml.isStartElement() && (xml.name() == "envelope")) {
            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.isEndElement())
                    break;
                if (xml.name() == "attack")
                    attackTime->setValue(xml.readElementText().toInt());
                else if (xml.name() == "release")
                    releaseTime->setValue(xml.readElementText().toInt());
                else skipXmlElement(xml);
             }
        }
        else if (xml.isStartElement() && (xml.name() == "midiControllers")) {
            midiControl->readData(xml);
        }
        else if (xml.isStartElement() && (xml.name() == "globalStores")) {
            parStore->readData(xml);
        }
        else skipXmlElement(xml);
    }
    modified = false;
}
#endif

void ArpWidget::updateChIn(int value)
{
    if (midiWorker) midiWorker->chIn = value;
    modified = true;
}

void ArpWidget::updateIndexIn(int value)
{
    if (indexIn[0] == sender()) {
        if (midiWorker) midiWorker->indexIn[0] = value;
    } else {
        if (midiWorker) midiWorker->indexIn[1] = value;
    }
    checkIfInputFilterSet();
    modified = true;
}

void ArpWidget::updateRangeIn(int value)
{
    if (rangeIn[0] == sender()) {
        if (midiWorker) midiWorker->rangeIn[0] = value;
    } else {
        if (midiWorker) midiWorker->rangeIn[1] = value;
    }
    checkIfInputFilterSet();
    modified = true;
}

void ArpWidget::updateText(const QString& newtext)
{
    patternPresetBox->setCurrentIndex(0);
    if (!midiWorker) return;
    textRemoveAction->setEnabled(false);
    textStoreAction->setEnabled(true);
    midiWorker->updatePattern(newtext);
    screen->updateScreen(newtext, midiWorker->minOctave,
                    midiWorker->maxOctave, midiWorker->minStepWidth,
                    midiWorker->nSteps, midiWorker->patternMaxIndex);

    modified = true;
}

void ArpWidget::selectPatternPreset(int val)
{
    if (val < patternPresets.count()) {
        if (val) {
            patternText->setText(patternPresets.at(val));
            if (!midiWorker) return;
            patternPresetBox->setCurrentIndex(val);
            textStoreAction->setEnabled(false);
            textRemoveAction->setEnabled(true);
        }
        else {
            if (!midiWorker) return;
            textRemoveAction->setEnabled(false);
        }
        modified = true;
    }
}

void ArpWidget::loadPatternPresets()
{
    QString qs;
    QStringList value;

    QDir qmahome = QDir(QDir::homePath());
    QString qmarcpath = qmahome.filePath(QMARCNAME);
    QFile f(qmarcpath);

    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, PACKAGE,
                tr("Could not read the pattern presets from the \n"
                   ".qmidiarprc resource file. To create this file \n"
                    "please just run the qmidiarp main application once."));
        return;
    }
    QTextStream loadText(&f);
    patternNames.clear();
    patternPresets.clear();

    while (!loadText.atEnd()) {
        qs = loadText.readLine();
        if (qs.startsWith('#')) {
            value.clear();
            value = qs.split('%');
            if ((value.at(0) == "#Pattern") && (value.count() > 2)) {
                patternNames << value.at(1);
                patternPresets << value.at(2);
            }
        }
    }
}

void ArpWidget::updateRepeatPattern(int val)
{
    if (midiWorker) midiWorker->repeatPatternThroughChord = val;
    modified = true;
}

void ArpWidget::updateOctaveMode(int val)
{
    if (midiWorker) midiWorker->updateOctaveMode(val);
    modified = true;
}

void ArpWidget::updateOctaveLow(int val)
{
    if (midiWorker) midiWorker->octLow = -val;
    modified = true;
}

void ArpWidget::updateOctaveHigh(int val)
{
    if (midiWorker) midiWorker->octHigh = val;
    modified = true;
}

void ArpWidget::updateEnableRestartByKbd(bool on)
{
    if (midiWorker) midiWorker->restartByKbd = on;
    modified = true;
}

void ArpWidget::updateEnableTrigByKbd(bool on)
{
    if (midiWorker) midiWorker->trigByKbd = on;
    modified = true;
}

void ArpWidget::updateTrigLegato(bool on)
{
    if (midiWorker) midiWorker->trigLegato = on;
    modified = true;
}

void ArpWidget::updateRandomLengthAmp(int val)
{
    if (midiWorker) midiWorker->updateRandomLengthAmp(val);
    checkIfRandomSet();
    modified = true;
}

void ArpWidget::updateRandomTickAmp(int val)
{
    if (midiWorker) midiWorker->updateRandomTickAmp(val);
    checkIfRandomSet();
    modified = true;
}

void ArpWidget::updateRandomVelocityAmp(int val)
{
    if (midiWorker) midiWorker->updateRandomVelocityAmp(val);
    checkIfRandomSet();
    modified = true;
}

void ArpWidget::checkIfRandomSet()
{
    if (randomLength->value() || randomTick->value()
                || randomVelocity->value()) {
        randomBox->setFlat(false);
        randomBox->setTitle(tr("Random - ACTIVE"));
    }
    else {
        randomBox->setFlat(true);
        randomBox->setTitle(tr("Random"));
    }
}

void ArpWidget::updateAttackTime(int val)
{
    if (midiWorker) midiWorker->updateAttackTime(val);
    checkIfEnvelopeSet();
    modified = true;
}

void ArpWidget::updateReleaseTime(int val)
{
    if (midiWorker) midiWorker->updateReleaseTime(val);
    checkIfEnvelopeSet();
    modified = true;
}

void ArpWidget::checkIfEnvelopeSet()
{
    if (attackTime->value() || releaseTime->value()) {
        envelopeBox->setFlat(false);
        envelopeBox->setTitle(tr("Envelope - ACTIVE"));
    }
    else {
        envelopeBox->setFlat(true);
        envelopeBox->setTitle(tr("Envelope"));
    }
}

void ArpWidget::openTextEditWindow(bool on)
{
    patternText->setHidden(!on);
}

void ArpWidget::storeCurrentPattern()
{
    QString qs;
    bool ok;

    qs = QInputDialog::getText(this, tr("%1: Store pattern").arg(PACKAGE),
            tr("New pattern"), QLineEdit::Normal, tr("Arp pattern"), &ok);

    if (ok && !qs.isEmpty()) {

        emit presetsChanged(qs, patternText->text(), 0);
        patternPresetBox->setCurrentIndex(patternNames.count() - 1);
        textRemoveAction->setEnabled(true);
    }
}

void ArpWidget::updatePatternPresets(const QString& n, const QString& p, int index)
{
    if (index) {
       if (index == patternPresetBox->currentIndex()) {
            patternPresetBox->setCurrentIndex(0);
            textRemoveAction->setEnabled(false);
        }
        patternNames.removeAt(index);
        patternPresets.removeAt(index);
        patternPresetBox->removeItem(index);
    } else {
        patternNames.append(n);
        patternPresets.append(p);
        patternPresetBox->addItem(n);
    }
}

void ArpWidget::removeCurrentPattern()
{
    QString qs;

    int currentIndex = patternPresetBox->currentIndex();
    if (currentIndex < 1) {
        return;
    }

    qs = tr("Remove \"%1\"?").arg(patternPresetBox->currentText());

    if (QMessageBox::question(0, PACKAGE, qs, QMessageBox::Yes,
                QMessageBox::No | QMessageBox::Default
                | QMessageBox::Escape, QMessageBox::NoButton)
            == QMessageBox::No) {
        return;
    }

    emit presetsChanged("", "", currentIndex);
}

void ArpWidget::setRandomVisible(bool on)
{
    randomTick->setVisible(on);
    randomVelocity->setVisible(on);
    randomLength->setVisible(on);
}

void ArpWidget::setEnvelopeVisible(bool on)
{
    attackTime->setVisible(on);
    releaseTime->setVisible(on);
}

void ArpWidget::setMuted(bool on)
{
    if (!midiWorker) return;
    midiWorker->setMuted(on);
    screen->setMuted(midiWorker->isMuted);
#ifdef APPBUILD
    parStore->ndc->setMuted(midiWorker->isMuted);
#endif
    modified = true;
}

void ArpWidget::updateDeferChanges(bool on)
{
    if (midiWorker) midiWorker->updateDeferChanges(on);
    modified = true;
}

void ArpWidget::setLatchMode(bool on)
{
    if (midiWorker) midiWorker->setLatchMode(on);
    modified = true;
}


void ArpWidget::updatePortOut(int value)
{
    if (midiWorker) midiWorker->portOut = value;
    modified = true;
}

void ArpWidget::updateChannelOut(int value)
{
    if (midiWorker) midiWorker->channelOut = value;
    modified = true;
}

void ArpWidget::storeParams(int ix, bool empty)
{
#ifdef APPBUILD
    // have to do this for moc not caring for APPBUILD flag
    doStoreParams(ix, empty);
#else
    (void)ix;
    (void)empty;
#endif
}

void ArpWidget::restoreParams(int ix)
{
#ifdef APPBUILD
    // have to do this for moc not caring for APPBUILD flag
    doRestoreParams(ix);
#else
    (void)ix;
#endif
}

#ifdef APPBUILD
bool ArpWidget::isModified()
{
    if (midiWorker)
        return (modified || midiControl->isModified());
    else
        return modified;
}

void ArpWidget::setModified(bool m)
{
    modified = m;
    if (midiWorker) midiControl->setModified(m);
}

void ArpWidget::doStoreParams(int ix, bool empty)
{
    parStore->temp.empty = empty;
    parStore->temp.muteOut = muteOut->isChecked();
    parStore->temp.chIn = chIn->currentIndex();
    parStore->temp.channelOut = channelOut->currentIndex();
    parStore->temp.portOut = portOut->currentIndex();
    parStore->temp.indexIn0 = indexIn[0]->value();
    parStore->temp.indexIn1 = indexIn[1]->value();
    parStore->temp.rangeIn0 = rangeIn[0]->value();
    parStore->temp.rangeIn1 = rangeIn[1]->value();
    parStore->temp.attack = attackTime->value();
    parStore->temp.release = releaseTime->value();
    parStore->temp.rndTick = randomTick->value();
    parStore->temp.rndLen = randomLength->value();
    parStore->temp.rndVel = randomVelocity->value();
    parStore->temp.pattern = patternText->text();
    parStore->temp.repeatMode = repeatPatternThroughChord->currentIndex();
    parStore->tempToList(ix);
}

void ArpWidget::doRestoreParams(int ix)
{
    midiWorker->applyPendingParChanges();
    if (parStore->list.at(ix).empty) return;
    patternText->setText(parStore->list.at(ix).pattern);
    repeatPatternThroughChord->setCurrentIndex(parStore->list.at(ix).repeatMode);
    updateRepeatPattern(parStore->list.at(ix).repeatMode);
    if (!parStore->onlyPatternList.at(ix)) {
        indexIn[0]->setValue(parStore->list.at(ix).indexIn0);
        indexIn[1]->setValue(parStore->list.at(ix).indexIn1);
        rangeIn[0]->setValue(parStore->list.at(ix).rangeIn0);
        rangeIn[1]->setValue(parStore->list.at(ix).rangeIn1);
        attackTime->setValue(parStore->list.at(ix).attack);
        releaseTime->setValue(parStore->list.at(ix).release);
        randomTick->setValue(parStore->list.at(ix).rndTick);
        randomLength->setValue(parStore->list.at(ix).rndLen);
        randomVelocity->setValue(parStore->list.at(ix).rndVel);

        //muteOut->setChecked(parStore->list.at(ix).muteOut);
        chIn->setCurrentIndex(parStore->list.at(ix).chIn);
        updateChIn(parStore->list.at(ix).chIn);
        channelOut->setCurrentIndex(parStore->list.at(ix).channelOut);
        updateChannelOut(parStore->list.at(ix).channelOut);
        setPortOut(parStore->list.at(ix).portOut);
        updatePortOut(parStore->list.at(ix).portOut);
    }
    midiWorker->advancePatternIndex(true);
}

void ArpWidget::handleController(int ccnumber, int channel, int value)
{
    bool m;
    int min, max, sval;
    QVector<MidiCC> cclist= midiControl->ccList;

    for (int l2 = 0; l2 < cclist.count(); l2++) {
        min = cclist.at(l2).min;
        max = cclist.at(l2).max;

        if ((ccnumber == cclist.at(l2).ccnumber) &&
            (channel == cclist.at(l2).channel)) {
            switch (cclist.at(l2).ID) {
                case 0: if (min == max) {
                            if (value == max) {
                                m = midiWorker->isMuted;
                                midiWorker->setMuted(!m);
                            }
                        }
                        else {
                            if (value == max) {
                                midiWorker->setMuted(false);
                            }
                            if (value == min) {
                                midiWorker->setMuted(true);
                            }
                        }
                break;
                case 1:
                        sval = min + ((double)value * (max - min) / 127);
                        patternPresetBoxIndex = sval;
                break;
                case 2:
                        sval = min + ((double)value * (max - min) / 127);
                        if ((sval < parStore->list.count())
                                && (sval != parStore->activeStore)
                                && (sval != parStore->currentRequest)) {
                            parStore->requestDispState(sval, 2);
                            parStore->restoreRequest = sval;
                            parStore->restoreRunOnce = (parStore->jumpToList.at(sval) > -2);
                        }
                        else return;
                break;

                default:
                break;
            }
            needsGUIUpdate = true;
        }
    }
}

void ArpWidget::updateDisplay()
{
    int frame = 1;
    if (getGrooveIndex() == 0 
            || getGrooveIndex() == midiWorker->nPoints) {
        frame = 0;
    }
    parStore->updateDisplay(frame, false);

    screen->updateDraw();
    midiControl->update();

    if (!(needsGUIUpdate || midiWorker->needsGUIUpdate)) return;

    muteOut->setChecked(midiWorker->isMuted);
    screen->setMuted(midiWorker->isMuted);
    parStore->ndc->setMuted(midiWorker->isMuted);
    if (patternPresetBoxIndex != patternPresetBox->currentIndex())
        selectPatternPreset(patternPresetBoxIndex);

    needsGUIUpdate = false;
    midiWorker->needsGUIUpdate = false;
}

void ArpWidget::skipXmlElement(QXmlStreamReader& xml)
{
    if (xml.isStartElement()) {
        qWarning("Unknown Element in XML File: %s",qPrintable(xml.name().toString()));
        while (!xml.atEnd()) {
            xml.readNext();

            if (xml.isEndElement())
                break;

            if (xml.isStartElement()) {
                skipXmlElement(xml);
            }
        }
    }
}
#endif
