#include "gerberviewerpage.h"

#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTextStream>
#include <QVBoxLayout>

#include "../common/filepicker.h"
#include "compositeview.h"
#include "excellonparser.h"
#include "gerberfiletype.h"
#include "gerberobjectpaths.h"
#include "gerberparser.h"
#include "layermaskrenderer.h"
#include "maskcompositor.h"

namespace {

// Classic CAM-viewer copper green for the single-layer view. The
// overlay compare milestone (M4) will bring per-layer colors; for one
// layer, one familiar color is enough.
const QColor kLayerColor(60, 220, 60);

} // namespace

GerberViewerPage::GerberViewerPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);

    auto *toolbar = new QHBoxLayout;
    auto *openButton = new QPushButton(tr("Open Gerber..."));
    statusLabel_ = new QLabel(tr("Open a Gerber layer file to view it."));
    toolbar->addWidget(openButton);
    toolbar->addWidget(statusLabel_, 1);
    layout->addLayout(toolbar);

    view_ = new CompositeView;
    view_->setMinimumSize(200, 200);
    layout->addWidget(view_, 1);

    connect(openButton, &QPushButton::clicked, this, &GerberViewerPage::openFile);
    connect(view_, &CompositeView::renderRequested, this, &GerberViewerPage::renderWindow);
}

void GerberViewerPage::openFile()
{
    // "All files" filter: real supplier archives use many extensions
    // (this project's own validation found .GER and Genesis-style
    // .G1/.GD1/... in real deliveries), so restricting the picker would
    // just get in the way. A non-Gerber file simply parses to nothing
    // drawable and reports that.
    const QString filePath = FilePicker::pickFile(this, tr("Open Gerber layer"), tr("All files (*)"));
    if (filePath.isEmpty()) {
        return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        statusLabel_->setText(tr("Could not read %1").arg(QFileInfo(filePath).fileName()));
        return;
    }
    QTextStream stream(&file);
    const QString content = stream.readAll();

    // Dispatch by extension, same rule as ArchiveComparePage::
    // parseGerberOrDrill - a drill file needs ExcellonParser, not
    // GerberParser, which doesn't speak Excellon syntax at all.
    if (GerberFileType::isDrillFile(filePath)) {
        try {
            layer_ = ExcellonParser::parseContentToObjects(content);
        } catch (const ExcellonParser::AmbiguousFormatError &error) {
            statusLabel_->setText(
                tr("%1: %2").arg(QFileInfo(filePath).fileName(), error.message()));
            return;
        }
    } else {
        layer_ = GerberParser::parseContentToObjects(content);
    }
    layerBoundsMm_ = GerberObjectPaths::layerBoundsMm(layer_);

    if (layerBoundsMm_.isEmpty()) {
        statusLabel_->setText(
            tr("%1: no drawable content found.").arg(QFileInfo(filePath).fileName()));
        return;
    }

    statusLabel_->setText(tr("%1 - %2 objects, %3 x %4 mm")
                              .arg(QFileInfo(filePath).fileName())
                              .arg(layer_.objects.size())
                              .arg(layerBoundsMm_.width(), 0, 'f', 1)
                              .arg(layerBoundsMm_.height(), 0, 'f', 1));
    view_->fitTo(layerBoundsMm_);
}

void GerberViewerPage::renderWindow(const QRectF &windowMm, double pxPerMm)
{
    if (layer_.objects.isEmpty()) {
        return;
    }
    // Synchronous by design for now: the constitution says measure
    // before threading, and the M3 gate on real files is exactly that
    // measurement. If dense layers make this stutter, LayerMaskRenderer
    // is already a pure function and moves to a worker thread unchanged.
    const LayerMask mask = LayerMaskRenderer::render(layer_, windowMm, pxPerMm);
    view_->setComposite(MaskCompositor::composite({{mask, kLayerColor}}), windowMm);
}
