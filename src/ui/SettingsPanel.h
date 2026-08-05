#pragma once

#include "core/JobSpec.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QSpinBox;

namespace dfu {

// Edits the selected job's JobSpec. Emits specEdited whenever the user changes
// anything; blocked while the panel is being populated so loading a job does
// not look like an edit.
class SettingsPanel : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsPanel(QWidget* parent = nullptr);

    void setSpec(const JobSpec& spec);
    JobSpec spec() const { return m_spec; }

    void setEditingEnabled(bool enabled);
    void setSourceResolution(int width, int height);
    void setSourceFps(int fpsNum, int fpsDen);

signals:
    void specEdited(const dfu::JobSpec& spec);

private:
    void buildUi();
    void pullFromWidgets();
    void pushToWidgets();
    void updateDerivedLabels();
    void emitEdit();

    JobSpec m_spec;
    bool m_populating = false;
    int m_sourceWidth = 0;
    int m_sourceHeight = 0;
    int m_sourceFpsNum = 0;
    int m_sourceFpsDen = 1;

    QCheckBox* m_deinterlace = nullptr;
    QComboBox* m_denoise = nullptr;
    QSlider* m_denoiseStrength = nullptr;
    QLabel* m_denoiseStrengthLabel = nullptr;
    QCheckBox* m_deblock = nullptr;
    QCheckBox* m_deband = nullptr;

    QComboBox* m_sharpen = nullptr;
    QSlider* m_sharpenStrength = nullptr;
    QLabel* m_sharpenStrengthLabel = nullptr;
    QSlider* m_brightness = nullptr;
    QSlider* m_contrast = nullptr;
    QSlider* m_saturation = nullptr;
    QLabel* m_brightnessLabel = nullptr;
    QLabel* m_contrastLabel = nullptr;
    QLabel* m_saturationLabel = nullptr;

    QCheckBox* m_interpolationEnabled = nullptr;
    QComboBox* m_fpsMultiplier = nullptr;
    QLabel* m_outputFpsLabel = nullptr;

    QCheckBox* m_upscaleEnabled = nullptr;
    QComboBox* m_upscaleFactor = nullptr;
    QComboBox* m_upscaleMethod = nullptr;
    QComboBox* m_upscaleModel = nullptr;
    QLabel* m_outputResolution = nullptr;

    QComboBox* m_encoder = nullptr;
    QSpinBox* m_quality = nullptr;
    QComboBox* m_container = nullptr;

    QLineEdit* m_outputPath = nullptr;
    QPushButton* m_browse = nullptr;
};

} // namespace dfu
