#include "ui/SettingsPanel.h"

#include "engine/ModelCatalog.h"
#include "engine/VulkanContext.h"
#include "media/FfmpegEncoder.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QStandardItemModel>
#include <QSpinBox>
#include <QVBoxLayout>

namespace dfu {

SettingsPanel::SettingsPanel(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
    pushToWidgets();
}

void SettingsPanel::buildUi()
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto* content = new QWidget(scroll);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(12);

    // ------------------------------------------------------------ restoration
    auto* restoreBox = new QGroupBox(tr("Restoration"), content);
    auto* restoreForm = new QFormLayout(restoreBox);

    m_deinterlace = new QCheckBox(tr("Deinterlace (bwdif)"), restoreBox);
    m_deinterlace->setToolTip(tr("Applied automatically when the source is detected as "
                                 "interlaced."));
    restoreForm->addRow(m_deinterlace);

    m_denoise = new QComboBox(restoreBox);
    m_denoise->addItem(tr("None"), QString());
    m_denoise->addItem(tr("hqdn3d - fast"), QStringLiteral("hqdn3d"));
    m_denoise->addItem(tr("atadenoise - temporal"), QStringLiteral("atadenoise"));
    m_denoise->addItem(tr("vaguedenoiser - wavelet"), QStringLiteral("vaguedenoiser"));
    m_denoise->addItem(tr("nlmeans - best, very slow"), QStringLiteral("nlmeans"));
    restoreForm->addRow(tr("Denoise:"), m_denoise);

    auto* strengthRow = new QWidget(restoreBox);
    auto* strengthLayout = new QHBoxLayout(strengthRow);
    strengthLayout->setContentsMargins(0, 0, 0, 0);
    m_denoiseStrength = new QSlider(Qt::Horizontal, strengthRow);
    m_denoiseStrength->setRange(1, 5);
    m_denoiseStrength->setPageStep(1);
    m_denoiseStrengthLabel = new QLabel(strengthRow);
    m_denoiseStrengthLabel->setMinimumWidth(16);
    strengthLayout->addWidget(m_denoiseStrength, 1);
    strengthLayout->addWidget(m_denoiseStrengthLabel);
    restoreForm->addRow(tr("Strength:"), strengthRow);

    m_deblock = new QCheckBox(tr("Deblock (compressed sources)"), restoreBox);
    m_deblock->setToolTip(tr("Removes the blocky edges left by heavy compression. Runs before "
                             "denoising, so the denoiser does not mistake block edges for "
                             "detail."));
    restoreForm->addRow(m_deblock);

    m_deband = new QCheckBox(tr("Deband (smooth gradients)"), restoreBox);
    m_deband->setToolTip(tr("Fixes the stepped bands that show up in skies and fades."));
    restoreForm->addRow(m_deband);

    layout->addWidget(restoreBox);

    // ---------------------------------------------------------------- upscale
    auto* upscaleBox = new QGroupBox(tr("Upscale"), content);
    auto* upscaleForm = new QFormLayout(upscaleBox);

    m_upscaleEnabled = new QCheckBox(tr("Enable upscaling"), upscaleBox);
    upscaleForm->addRow(m_upscaleEnabled);

    m_upscaleFactor = new QComboBox(upscaleBox);
    m_upscaleFactor->addItem(tr("2x"), 2);
    m_upscaleFactor->addItem(tr("3x"), 3);
    m_upscaleFactor->addItem(tr("4x"), 4);
    upscaleForm->addRow(tr("Factor:"), m_upscaleFactor);

    m_upscaleMethod = new QComboBox(upscaleBox);
    m_upscaleMethod->addItem(tr("Real-ESRGAN (AI, GPU)"),
                             upscaleMethodKey(UpscaleMethod::RealEsrganNcnn));
    m_upscaleMethod->addItem(tr("Lanczos (FFmpeg, fast)"),
                             upscaleMethodKey(UpscaleMethod::FfmpegLanczos));
    m_upscaleMethod->addItem(tr("Spline (FFmpeg, smoother)"),
                             upscaleMethodKey(UpscaleMethod::FfmpegSpline));

    // Probing the driver once at construction: without a usable Vulkan device
    // the AI entry stays visible but disabled, with the reason attached.
    // Hiding it would leave users hunting for a feature that is advertised.
    const bool vulkanOk = VulkanContext::vulkanAvailable();
    if (!vulkanOk) {
        if (auto* model = qobject_cast<QStandardItemModel*>(m_upscaleMethod->model())) {
            if (QStandardItem* item = model->item(0)) {
                item->setEnabled(false);
            }
        }
        m_upscaleMethod->setItemText(0, tr("Real-ESRGAN (AI) - no Vulkan device"));
        m_upscaleMethod->setToolTip(
            tr("No Vulkan-capable GPU was detected. Update the graphics driver to enable the "
               "AI upscaler."));
    }
    upscaleForm->addRow(tr("Method:"), m_upscaleMethod);

    // Populated from whatever is actually in the models folder, so dropping a
    // converted ncnn model in there is enough to make it selectable.
    m_upscaleModel = new QComboBox(upscaleBox);
    m_models = discoverUpscaleModels();
    for (const UpscaleModelInfo& model : m_models) {
        m_upscaleModel->addItem(model.displayName, model.name);
        if (!model.description.isEmpty()) {
            m_upscaleModel->setItemData(m_upscaleModel->count() - 1, model.description,
                                        Qt::ToolTipRole);
        }
    }
    if (m_models.isEmpty()) {
        m_upscaleModel->addItem(tr("No models found in the models folder"), QString());
        m_upscaleModel->setEnabled(false);
    }
    upscaleForm->addRow(tr("AI model:"), m_upscaleModel);

    m_outputResolution = new QLabel(upscaleBox);
    m_outputResolution->setEnabled(false);
    upscaleForm->addRow(tr("Output:"), m_outputResolution);

    layout->addWidget(upscaleBox);

    // ------------------------------------------------------------ enhancement
    auto* enhanceBox = new QGroupBox(tr("Enhancement (after upscaling)"), content);
    auto* enhanceForm = new QFormLayout(enhanceBox);

    m_sharpen = new QComboBox(enhanceBox);
    m_sharpen->addItem(tr("None"), QString());
    m_sharpen->addItem(tr("CAS - adaptive, no halos"), QStringLiteral("cas"));
    m_sharpen->addItem(tr("Unsharp mask - stronger"), QStringLiteral("unsharp"));
    m_sharpen->setToolTip(tr("Sharpening runs after the upscaler. Doing it first would have the "
                             "network amplify the halos it introduces."));
    enhanceForm->addRow(tr("Sharpen:"), m_sharpen);

    auto* sharpRow = new QWidget(enhanceBox);
    auto* sharpLayout = new QHBoxLayout(sharpRow);
    sharpLayout->setContentsMargins(0, 0, 0, 0);
    m_sharpenStrength = new QSlider(Qt::Horizontal, sharpRow);
    m_sharpenStrength->setRange(1, 5);
    m_sharpenStrengthLabel = new QLabel(sharpRow);
    m_sharpenStrengthLabel->setMinimumWidth(28);
    sharpLayout->addWidget(m_sharpenStrength, 1);
    sharpLayout->addWidget(m_sharpenStrengthLabel);
    enhanceForm->addRow(tr("Amount:"), sharpRow);

    struct ColourRow
    {
        QSlider** slider;
        QLabel** label;
        QString title;
    };
    const ColourRow colourRows[] = {
        {&m_brightness, &m_brightnessLabel, tr("Brightness:")},
        {&m_contrast, &m_contrastLabel, tr("Contrast:")},
        {&m_saturation, &m_saturationLabel, tr("Saturation:")},
    };

    for (const ColourRow& row : colourRows) {
        auto* holder = new QWidget(enhanceBox);
        auto* holderLayout = new QHBoxLayout(holder);
        holderLayout->setContentsMargins(0, 0, 0, 0);

        auto* slider = new QSlider(Qt::Horizontal, holder);
        slider->setRange(-50, 50);
        slider->setPageStep(5);
        auto* label = new QLabel(holder);
        label->setMinimumWidth(28);

        holderLayout->addWidget(slider, 1);
        holderLayout->addWidget(label);
        enhanceForm->addRow(row.title, holder);

        *row.slider = slider;
        *row.label = label;
    }

    layout->addWidget(enhanceBox);

    // --------------------------------------------------------- interpolation
    auto* fpsBox = new QGroupBox(tr("Frame interpolation"), content);
    auto* fpsForm = new QFormLayout(fpsBox);

    m_interpolationEnabled = new QCheckBox(tr("Increase frame rate (RIFE)"), fpsBox);
    m_interpolationEnabled->setToolTip(
        tr("Generates in-between frames on the GPU for smoother motion. Costs roughly one extra "
           "inference per generated frame."));
    fpsForm->addRow(m_interpolationEnabled);

    m_fpsMultiplier = new QComboBox(fpsBox);
    m_fpsMultiplier->addItem(tr("2x"), 2);
    m_fpsMultiplier->addItem(tr("3x"), 3);
    m_fpsMultiplier->addItem(tr("4x"), 4);
    fpsForm->addRow(tr("Multiplier:"), m_fpsMultiplier);

    m_outputFpsLabel = new QLabel(fpsBox);
    m_outputFpsLabel->setEnabled(false);
    fpsForm->addRow(tr("Output rate:"), m_outputFpsLabel);

    layout->addWidget(fpsBox);

    // ----------------------------------------------------------------- encode
    auto* encodeBox = new QGroupBox(tr("Encode"), content);
    auto* encodeForm = new QFormLayout(encodeBox);

    m_encoder = new QComboBox(encodeBox);
    const QStringList encoders = availableEncoders();
    for (const QString& encoder : encoders) {
        m_encoder->addItem(encoderDisplayName(encoder), encoder);
    }
    encodeForm->addRow(tr("Encoder:"), m_encoder);

    m_quality = new QSpinBox(encodeBox);
    m_quality->setRange(0, 51);
    m_quality->setToolTip(tr("Lower is better quality and a larger file. 20 is a good default; "
                             "18 is close to visually lossless."));
    encodeForm->addRow(tr("Quality (CQ/CRF):"), m_quality);

    m_container = new QComboBox(encodeBox);
    m_container->addItem(QStringLiteral("mp4"), QStringLiteral("mp4"));
    m_container->addItem(QStringLiteral("mkv"), QStringLiteral("mkv"));
    encodeForm->addRow(tr("Container:"), m_container);

    layout->addWidget(encodeBox);

    // ----------------------------------------------------------------- output
    auto* outputBox = new QGroupBox(tr("Output file"), content);
    auto* outputLayout = new QHBoxLayout(outputBox);
    m_outputPath = new QLineEdit(outputBox);
    m_browse = new QPushButton(tr("..."), outputBox);
    m_browse->setFixedWidth(32);
    outputLayout->addWidget(m_outputPath, 1);
    outputLayout->addWidget(m_browse);
    layout->addWidget(outputBox);

    layout->addStretch(1);

    scroll->setWidget(content);
    outer->addWidget(scroll);

    // -------------------------------------------------------------- behaviour
    connect(m_deinterlace, &QCheckBox::toggled, this, &SettingsPanel::emitEdit);
    connect(m_deblock, &QCheckBox::toggled, this, &SettingsPanel::emitEdit);
    connect(m_deband, &QCheckBox::toggled, this, &SettingsPanel::emitEdit);
    connect(m_sharpen, &QComboBox::currentIndexChanged, this, &SettingsPanel::emitEdit);
    connect(m_sharpenStrength, &QSlider::valueChanged, this, &SettingsPanel::emitEdit);
    connect(m_brightness, &QSlider::valueChanged, this, &SettingsPanel::emitEdit);
    connect(m_contrast, &QSlider::valueChanged, this, &SettingsPanel::emitEdit);
    connect(m_saturation, &QSlider::valueChanged, this, &SettingsPanel::emitEdit);
    connect(m_interpolationEnabled, &QCheckBox::toggled, this, &SettingsPanel::emitEdit);
    connect(m_fpsMultiplier, &QComboBox::currentIndexChanged, this, &SettingsPanel::emitEdit);
    connect(m_denoise, &QComboBox::currentIndexChanged, this, &SettingsPanel::emitEdit);
    connect(m_denoiseStrength, &QSlider::valueChanged, this, &SettingsPanel::emitEdit);
    connect(m_upscaleEnabled, &QCheckBox::toggled, this, &SettingsPanel::emitEdit);
    connect(m_upscaleFactor, &QComboBox::currentIndexChanged, this, &SettingsPanel::emitEdit);
    connect(m_upscaleMethod, &QComboBox::currentIndexChanged, this, &SettingsPanel::emitEdit);
    connect(m_upscaleModel, &QComboBox::currentIndexChanged, this, &SettingsPanel::emitEdit);
    connect(m_encoder, &QComboBox::currentIndexChanged, this, &SettingsPanel::emitEdit);
    connect(m_quality, &QSpinBox::valueChanged, this, &SettingsPanel::emitEdit);
    connect(m_container, &QComboBox::currentIndexChanged, this, &SettingsPanel::emitEdit);
    connect(m_outputPath, &QLineEdit::editingFinished, this, &SettingsPanel::emitEdit);

    connect(m_browse, &QPushButton::clicked, this, [this]() {
        const QString chosen = QFileDialog::getSaveFileName(
            this, tr("Choose the output file"), m_outputPath->text(),
            tr("Video files (*.mp4 *.mkv);;All files (*)"));
        if (!chosen.isEmpty()) {
            m_outputPath->setText(chosen);
            emitEdit();
        }
    });
}

void SettingsPanel::setSpec(const JobSpec& spec)
{
    m_spec = spec;
    pushToWidgets();
}

void SettingsPanel::setSourceResolution(int width, int height)
{
    m_sourceWidth = width;
    m_sourceHeight = height;
    updateDerivedLabels();
}

void SettingsPanel::setEditingEnabled(bool enabled)
{
    setEnabled(enabled);
}

void SettingsPanel::pushToWidgets()
{
    m_populating = true;

    m_deinterlace->setChecked(m_spec.deinterlace);
    m_deblock->setChecked(m_spec.deblock);
    m_deband->setChecked(m_spec.deband);

    const int sharpenIndex = m_sharpen->findData(m_spec.sharpenFilter);
    m_sharpen->setCurrentIndex(sharpenIndex >= 0 ? sharpenIndex : 0);
    m_sharpenStrength->setValue(m_spec.sharpenStrength);
    m_brightness->setValue(m_spec.brightness);
    m_contrast->setValue(m_spec.contrast);
    m_saturation->setValue(m_spec.saturation);

    m_interpolationEnabled->setChecked(m_spec.interpolationEnabled);
    const int multiplierIndex = m_fpsMultiplier->findData(m_spec.fpsMultiplier);
    m_fpsMultiplier->setCurrentIndex(multiplierIndex >= 0 ? multiplierIndex : 0);

    const int denoiseIndex = m_denoise->findData(m_spec.denoiseFilter);
    m_denoise->setCurrentIndex(denoiseIndex >= 0 ? denoiseIndex : 0);
    m_denoiseStrength->setValue(m_spec.denoiseStrength);

    m_upscaleEnabled->setChecked(m_spec.upscaleEnabled);
    const int factorIndex = m_upscaleFactor->findData(m_spec.upscaleFactor);
    m_upscaleFactor->setCurrentIndex(factorIndex >= 0 ? factorIndex : 0);
    const int methodIndex = m_upscaleMethod->findData(upscaleMethodKey(m_spec.upscaleMethod));
    m_upscaleMethod->setCurrentIndex(methodIndex >= 0 ? methodIndex : 0);
    const int modelIndex = m_upscaleModel->findData(m_spec.upscaleModel);
    m_upscaleModel->setCurrentIndex(modelIndex >= 0 ? modelIndex : 0);

    const int encoderIndex = m_encoder->findData(m_spec.encoder);
    m_encoder->setCurrentIndex(encoderIndex >= 0 ? encoderIndex : 0);
    m_quality->setValue(m_spec.quality);
    const int containerIndex = m_container->findData(m_spec.container);
    m_container->setCurrentIndex(containerIndex >= 0 ? containerIndex : 0);

    m_outputPath->setText(m_spec.outputPath);

    m_populating = false;
    updateDerivedLabels();
}

void SettingsPanel::pullFromWidgets()
{
    m_spec.deinterlace = m_deinterlace->isChecked();
    m_spec.deblock = m_deblock->isChecked();
    m_spec.deband = m_deband->isChecked();

    m_spec.sharpenFilter = m_sharpen->currentData().toString();
    m_spec.sharpenStrength = m_sharpenStrength->value();
    m_spec.brightness = m_brightness->value();
    m_spec.contrast = m_contrast->value();
    m_spec.saturation = m_saturation->value();

    m_spec.interpolationEnabled = m_interpolationEnabled->isChecked();
    m_spec.fpsMultiplier = m_fpsMultiplier->currentData().toInt();
    m_spec.denoiseFilter = m_denoise->currentData().toString();
    m_spec.denoiseStrength = m_denoiseStrength->value();

    m_spec.upscaleEnabled = m_upscaleEnabled->isChecked();
    m_spec.upscaleFactor = m_upscaleFactor->currentData().toInt();
    m_spec.upscaleMethod = upscaleMethodFromKey(m_upscaleMethod->currentData().toString());
    m_spec.upscaleModel = m_upscaleModel->currentData().toString();
    // The factor is constrained to the scales the selected model ships, in
    // syncFactorOptions.

    m_spec.encoder = m_encoder->currentData().toString();
    m_spec.quality = m_quality->value();
    m_spec.container = m_container->currentData().toString();

    m_spec.outputPath = m_outputPath->text();

    // Keep the extension honest when the container changes.
    if (!m_spec.outputPath.isEmpty()) {
        const QFileInfo info(m_spec.outputPath);
        if (info.suffix().compare(m_spec.container, Qt::CaseInsensitive) != 0) {
            m_spec.outputPath =
                info.absolutePath() + QLatin1Char('/') + info.completeBaseName()
                + QLatin1Char('.') + m_spec.container;
        }
    }
}

void SettingsPanel::syncFactorOptions(bool aiSelected)
{
    // The factors offered come from the weights that exist on disk. A model
    // that only ships x4 must not let the user pick x2 and then fail at load.
    QList<int> scales{2, 3, 4};

    if (aiSelected) {
        const QString modelName = m_upscaleModel->currentData().toString();
        for (const UpscaleModelInfo& model : m_models) {
            if (model.name == modelName) {
                scales = model.scales;
                break;
            }
        }
    }

    if (scales.isEmpty()) {
        scales = {4};
    }

    QList<int> current;
    current.reserve(m_upscaleFactor->count());
    for (int i = 0; i < m_upscaleFactor->count(); ++i) {
        current.append(m_upscaleFactor->itemData(i).toInt());
    }
    if (current == scales) {
        m_upscaleFactor->setToolTip(scales.size() == 1
                                        ? tr("This model only provides x%1 weights.").arg(scales.first())
                                        : QString());
        return;
    }

    const int wanted = m_upscaleFactor->currentData().toInt();

    const bool blocked = m_populating;
    m_populating = true;
    m_upscaleFactor->clear();
    for (int scale : scales) {
        m_upscaleFactor->addItem(tr("%1x").arg(scale), scale);
    }
    const int index = m_upscaleFactor->findData(wanted);
    m_upscaleFactor->setCurrentIndex(index >= 0 ? index : 0);
    m_populating = blocked;

    m_spec.upscaleFactor = m_upscaleFactor->currentData().toInt();

    m_upscaleFactor->setToolTip(scales.size() == 1
                                    ? tr("This model only provides x%1 weights.").arg(scales.first())
                                    : QString());
}

void SettingsPanel::setSourceFps(int fpsNum, int fpsDen)
{
    m_sourceFpsNum = fpsNum;
    m_sourceFpsDen = fpsDen > 0 ? fpsDen : 1;
    updateDerivedLabels();
}

void SettingsPanel::updateDerivedLabels()
{
    m_denoiseStrengthLabel->setText(QString::number(m_denoiseStrength->value()));

    const bool denoiseActive = !m_denoise->currentData().toString().isEmpty();
    m_denoiseStrength->setEnabled(denoiseActive);
    m_denoiseStrengthLabel->setEnabled(denoiseActive);

    // Enhancement
    m_sharpenStrengthLabel->setText(QString::number(m_sharpenStrength->value()));
    const bool sharpenActive = !m_sharpen->currentData().toString().isEmpty();
    m_sharpenStrength->setEnabled(sharpenActive);
    m_sharpenStrengthLabel->setEnabled(sharpenActive);

    const auto signedText = [](int value) {
        return value > 0 ? QStringLiteral("+%1").arg(value) : QString::number(value);
    };
    m_brightnessLabel->setText(signedText(m_brightness->value()));
    m_contrastLabel->setText(signedText(m_contrast->value()));
    m_saturationLabel->setText(signedText(m_saturation->value()));

    // Interpolation
    const bool interpolating = m_interpolationEnabled->isChecked();
    m_fpsMultiplier->setEnabled(interpolating);

    if (m_sourceFpsNum > 0) {
        const int multiplier = interpolating ? m_fpsMultiplier->currentData().toInt() : 1;
        const double outputFps =
            static_cast<double>(m_sourceFpsNum * multiplier) / m_sourceFpsDen;
        m_outputFpsLabel->setText(QStringLiteral("%1 fps (%2/%3)")
                                      .arg(QString::number(outputFps, 'f', 3),
                                           QString::number(m_sourceFpsNum * multiplier),
                                           QString::number(m_sourceFpsDen)));
    } else {
        m_outputFpsLabel->setText(QStringLiteral("-"));
    }

    const bool upscaling = m_upscaleEnabled->isChecked();
    const bool ai = upscaling
                    && upscaleMethodFromKey(m_upscaleMethod->currentData().toString())
                           == UpscaleMethod::RealEsrganNcnn;

    m_upscaleMethod->setEnabled(upscaling);
    m_upscaleModel->setEnabled(ai && !m_models.isEmpty());
    m_upscaleFactor->setEnabled(upscaling);

    syncFactorOptions(ai);

    if (m_sourceWidth > 0 && m_sourceHeight > 0) {
        const int factor = upscaling ? m_upscaleFactor->currentData().toInt() : 1;
        m_outputResolution->setText(QStringLiteral("%1 x %2")
                                        .arg((m_sourceWidth * factor) & ~1)
                                        .arg((m_sourceHeight * factor) & ~1));
    } else {
        m_outputResolution->setText(QStringLiteral("-"));
    }
}

void SettingsPanel::emitEdit()
{
    if (m_populating) {
        return;
    }
    pullFromWidgets();
    updateDerivedLabels();

    m_populating = true;
    m_outputPath->setText(m_spec.outputPath);
    m_populating = false;

    emit specEdited(m_spec);
}

} // namespace dfu
