#include "component_property_widget.h"
#include "float_number_widget.h"
#include "image_forever.h"
#include "image_preview_widget.h"
#include "theme.h"
#include <QColorDialog>
#include <QComboBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>
#include <unordered_set>

ComponentPropertyWidget::ComponentPropertyWidget(Document* document,
    const std::vector<dust3d::Uuid>& componentIds,
    QWidget* parent)
    : QWidget(parent)
    , m_document(document)
    , m_componentIds(componentIds)
{
    preparePartIds();
    m_color = lastColor();

    QPushButton* colorPreviewArea = new QPushButton;
    colorPreviewArea->setStyleSheet("QPushButton {background-color: " + m_color.name() + "; border-radius: 0;}");
    colorPreviewArea->setFixedSize(Theme::toolIconSize * 1.8, Theme::toolIconSize);

    QPushButton* colorPickerButton = new QPushButton(Theme::awesome()->icon(fa::eyedropper), "");
    Theme::initIconButton(colorPickerButton);
    connect(colorPickerButton, &QPushButton::clicked, this, &ComponentPropertyWidget::showColorDialog);

    QComboBox* combineModeSelectBox = nullptr;
    std::set<dust3d::CombineMode> combineModes;
    for (const auto& componentId : m_componentIds) {
        const Document::Component* oneComponent = m_document->findComponent(componentId);
        if (nullptr == oneComponent)
            continue;
        combineModes.insert(oneComponent->combineMode);
    }
    if (!combineModes.empty()) {
        int startIndex = (1 == combineModes.size()) ? 0 : 1;
        combineModeSelectBox = new QComboBox;
        if (0 != startIndex)
            combineModeSelectBox->addItem(tr("Not Change"));
        for (size_t i = 0; i < (size_t)dust3d::CombineMode::Count; ++i) {
            dust3d::CombineMode mode = (dust3d::CombineMode)i;
            combineModeSelectBox->addItem(QString::fromStdString(dust3d::CombineModeToDispName(mode)));
        }
        if (0 != startIndex)
            combineModeSelectBox->setCurrentIndex(0);
        else
            combineModeSelectBox->setCurrentIndex((int)*combineModes.begin());
        connect(combineModeSelectBox, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [=](int index) {
            if (index < startIndex)
                return;
            for (const auto& componentId : m_componentIds) {
                emit setComponentCombineMode(componentId, (dust3d::CombineMode)(index - startIndex));
            }
            emit groupOperationAdded();
        });
    }

    QHBoxLayout* topLayout = new QHBoxLayout;
    topLayout->addWidget(colorPreviewArea);
    topLayout->addWidget(colorPickerButton);
    topLayout->addStretch();
    if (nullptr != combineModeSelectBox)
        topLayout->addWidget(combineModeSelectBox);
    topLayout->setSizeConstraint(QLayout::SetFixedSize);

    QGroupBox* deformGroupBox = nullptr;
    if (nullptr != m_part) {
        FloatNumberWidget* thicknessWidget = new FloatNumberWidget;
        thicknessWidget->setItemName(tr("Thickness"));
        thicknessWidget->setRange(0, 2);
        thicknessWidget->setValue(m_part->deformThickness);

        connect(thicknessWidget, &FloatNumberWidget::valueChanged, [=](float value) {
            emit setPartDeformThickness(m_partId, value);
            emit groupOperationAdded();
        });

        FloatNumberWidget* widthWidget = new FloatNumberWidget;
        widthWidget->setItemName(tr("Width"));
        widthWidget->setRange(0, 2);
        widthWidget->setValue(m_part->deformWidth);

        connect(widthWidget, &FloatNumberWidget::valueChanged, [=](float value) {
            emit setPartDeformWidth(m_partId, value);
            emit groupOperationAdded();
        });

        QPushButton* thicknessEraser = new QPushButton(Theme::awesome()->icon(fa::eraser), "");
        Theme::initIconButton(thicknessEraser);

        connect(thicknessEraser, &QPushButton::clicked, [=]() {
            thicknessWidget->setValue(1.0);
            emit groupOperationAdded();
        });

        QPushButton* widthEraser = new QPushButton(Theme::awesome()->icon(fa::eraser), "");
        Theme::initIconButton(widthEraser);

        connect(widthEraser, &QPushButton::clicked, [=]() {
            widthWidget->setValue(1.0);
            emit groupOperationAdded();
        });

        QVBoxLayout* deformLayout = new QVBoxLayout;

        QHBoxLayout* thicknessLayout = new QHBoxLayout;
        QHBoxLayout* widthLayout = new QHBoxLayout;
        thicknessLayout->addWidget(thicknessEraser);
        thicknessLayout->addWidget(thicknessWidget);
        widthLayout->addWidget(widthEraser);
        widthLayout->addWidget(widthWidget);

        QCheckBox* deformUnifyStateBox = new QCheckBox();
        Theme::initCheckbox(deformUnifyStateBox);
        deformUnifyStateBox->setText(tr("Unified"));
        deformUnifyStateBox->setChecked(m_part->deformUnified);

        connect(deformUnifyStateBox, &QCheckBox::stateChanged, this, [=]() {
            emit setPartDeformUnified(m_partId, deformUnifyStateBox->isChecked());
            emit groupOperationAdded();
        });

        QHBoxLayout* deformUnifyLayout = new QHBoxLayout;
        deformUnifyLayout->addStretch();
        deformUnifyLayout->addWidget(deformUnifyStateBox);

        deformLayout->addLayout(thicknessLayout);
        deformLayout->addLayout(widthLayout);
        deformLayout->addLayout(deformUnifyLayout);

        deformGroupBox = new QGroupBox(tr("Deform"));
        deformGroupBox->setLayout(deformLayout);
    }

    QGroupBox* cutFaceGroupBox = nullptr;
    if (nullptr != m_part) {
        FloatNumberWidget* rotationWidget = new FloatNumberWidget;
        rotationWidget->setItemName(tr("Rotation"));
        rotationWidget->setRange(-1, 1);
        rotationWidget->setValue(m_part->cutRotation);

        connect(rotationWidget, &FloatNumberWidget::valueChanged, [=](float value) {
            emit setPartCutRotation(m_partId, value);
            emit groupOperationAdded();
        });

        QPushButton* rotationEraser = new QPushButton(Theme::awesome()->icon(fa::eraser), "");
        Theme::initIconButton(rotationEraser);

        connect(rotationEraser, &QPushButton::clicked, [=]() {
            rotationWidget->setValue(0.0);
            emit groupOperationAdded();
        });

        QPushButton* rotationMinus5Button = new QPushButton(Theme::awesome()->icon(fa::rotateleft), "");
        Theme::initIconButton(rotationMinus5Button);

        connect(rotationMinus5Button, &QPushButton::clicked, [=]() {
            rotationWidget->setValue(-0.5);
            emit groupOperationAdded();
        });

        QPushButton* rotation5Button = new QPushButton(Theme::awesome()->icon(fa::rotateright), "");
        Theme::initIconButton(rotation5Button);

        connect(rotation5Button, &QPushButton::clicked, [=]() {
            rotationWidget->setValue(0.5);
            emit groupOperationAdded();
        });

        QHBoxLayout* rotationLayout = new QHBoxLayout;
        rotationLayout->addWidget(rotationEraser);
        rotationLayout->addWidget(rotationWidget);
        rotationLayout->addWidget(rotationMinus5Button);
        rotationLayout->addWidget(rotation5Button);

        QCheckBox* subdivStateBox = new QCheckBox();
        Theme::initCheckbox(subdivStateBox);
        subdivStateBox->setText(tr("Subdivided"));
        subdivStateBox->setChecked(m_part->subdived);

        connect(subdivStateBox, &QCheckBox::stateChanged, this, [=]() {
            emit setPartSubdivState(m_partId, subdivStateBox->isChecked());
            emit groupOperationAdded();
        });

        QCheckBox* chamferStateBox = new QCheckBox();
        Theme::initCheckbox(chamferStateBox);
        chamferStateBox->setText(tr("Chamfered"));
        chamferStateBox->setChecked(m_part->chamfered);

        connect(chamferStateBox, &QCheckBox::stateChanged, this, [=]() {
            emit setPartChamferState(m_partId, chamferStateBox->isChecked());
            emit groupOperationAdded();
        });

        QCheckBox* roundEndStateBox = new QCheckBox();
        Theme::initCheckbox(roundEndStateBox);
        roundEndStateBox->setText(tr("Round end"));
        roundEndStateBox->setChecked(m_part->rounded);

        connect(roundEndStateBox, &QCheckBox::stateChanged, this, [=]() {
            emit setPartRoundState(m_partId, roundEndStateBox->isChecked());
            emit groupOperationAdded();
        });

        QHBoxLayout* optionsLayout = new QHBoxLayout;
        optionsLayout->addStretch();
        optionsLayout->addWidget(roundEndStateBox);
        optionsLayout->addWidget(chamferStateBox);
        optionsLayout->addWidget(subdivStateBox);

        QVBoxLayout* cutFaceLayout = new QVBoxLayout;
        cutFaceLayout->addLayout(rotationLayout);
        cutFaceLayout->addLayout(optionsLayout);

        cutFaceGroupBox = new QGroupBox(tr("Cut Face"));
        cutFaceGroupBox->setLayout(cutFaceLayout);
    }

    QGroupBox* colorImageGroupBox = nullptr;
    if (!m_componentIds.empty()) {
        ImagePreviewWidget* colorImagePreviewWidget = new ImagePreviewWidget;
        colorImagePreviewWidget->setFixedSize(Theme::partPreviewImageSize * 2, Theme::partPreviewImageSize * 2);
        auto colorImageId = lastColorImageId();
        colorImagePreviewWidget->updateImage(colorImageId.isNull() ? QImage() : *ImageForever::get(colorImageId));
        QPushButton* colorImageEraser = new QPushButton(Theme::awesome()->icon(fa::eraser), "");
        Theme::initIconButton(colorImageEraser);

        connect(colorImageEraser, &QPushButton::clicked, [=]() {
            for (const auto& partId : m_partIds)
                emit setPartColorImage(partId, dust3d::Uuid());
            emit groupOperationAdded();
        });

        QPushButton* colorImagePicker = new QPushButton(Theme::awesome()->icon(fa::image), "");
        Theme::initIconButton(colorImagePicker);

        connect(colorImagePicker, &QPushButton::clicked, [=]() {
            QImage* image = pickImage();
            if (nullptr == image)
                return;
            auto imageId = ImageForever::add(image);
            delete image;
            for (const auto& partId : m_partIds)
                emit setPartColorImage(partId, imageId);
            emit groupOperationAdded();
        });

        QHBoxLayout* colorImageToolsLayout = new QHBoxLayout;
        colorImageToolsLayout->addWidget(colorImageEraser);
        colorImageToolsLayout->addWidget(colorImagePicker);
        colorImageToolsLayout->addStretch();

        QVBoxLayout* colorImageLayout = new QVBoxLayout;
        colorImageLayout->addWidget(colorImagePreviewWidget);
        colorImageLayout->addLayout(colorImageToolsLayout);

        QHBoxLayout* skinLayout = new QHBoxLayout;
        skinLayout->addLayout(colorImageLayout);
        skinLayout->addStretch();

        colorImageGroupBox = new QGroupBox(tr("Texture Image"));
        colorImageGroupBox->setLayout(skinLayout);
    }

    QHBoxLayout* skinLayout = new QHBoxLayout;
    if (nullptr != colorImageGroupBox)
        skinLayout->addWidget(colorImageGroupBox);

    QVBoxLayout* mainLayout = new QVBoxLayout;
    mainLayout->addLayout(topLayout);
    if (nullptr != deformGroupBox)
        mainLayout->addWidget(deformGroupBox);
    if (nullptr != cutFaceGroupBox)
        mainLayout->addWidget(cutFaceGroupBox);
    mainLayout->addLayout(skinLayout);
    mainLayout->setSizeConstraint(QLayout::SetFixedSize);

    connect(this, &ComponentPropertyWidget::setPartColorState, m_document, &Document::setPartColorState);
    connect(this, &ComponentPropertyWidget::endColorPicking, m_document, &Document::enableBackgroundBlur);
    connect(this, &ComponentPropertyWidget::beginColorPicking, m_document, &Document::disableBackgroundBlur);
    connect(this, &ComponentPropertyWidget::setPartDeformThickness, m_document, &Document::setPartDeformThickness);
    connect(this, &ComponentPropertyWidget::setPartDeformWidth, m_document, &Document::setPartDeformWidth);
    connect(this, &ComponentPropertyWidget::setPartDeformUnified, m_document, &Document::setPartDeformUnified);
    connect(this, &ComponentPropertyWidget::setPartCutRotation, m_document, &Document::setPartCutRotation);
    connect(this, &ComponentPropertyWidget::setPartSubdivState, m_document, &Document::setPartSubdivState);
    connect(this, &ComponentPropertyWidget::setPartChamferState, m_document, &Document::setPartChamferState);
    connect(this, &ComponentPropertyWidget::setPartRoundState, m_document, &Document::setPartRoundState);
    connect(this, &ComponentPropertyWidget::setPartColorImage, m_document, &Document::setPartColorImage);
    connect(this, &ComponentPropertyWidget::setComponentCombineMode, m_document, &Document::setComponentCombineMode);
    connect(this, &ComponentPropertyWidget::groupOperationAdded, m_document, &Document::saveSnapshot);

    setLayout(mainLayout);

    setFixedSize(minimumSizeHint());
}

QImage* ComponentPropertyWidget::pickImage()
{
    QString fileName = QFileDialog::getOpenFileName(this, QString(), QString(),
        tr("Image Files (*.png *.jpg *.bmp)"))
                           .trimmed();
    if (fileName.isEmpty())
        return nullptr;
    QImage* image = new QImage();
    if (!image->load(fileName))
        return nullptr;
    return image;
}

void ComponentPropertyWidget::preparePartIds()
{
    std::unordered_set<dust3d::Uuid> addedPartIdSet;
    for (const auto& componentId : m_componentIds) {
        std::vector<dust3d::Uuid> partIds;
        m_document->collectComponentDescendantParts(componentId, partIds);
        for (const auto& it : partIds) {
            if (addedPartIdSet.insert(it).second)
                m_partIds.emplace_back(it);
        }
    }
    if (1 == m_partIds.size()) {
        m_part = m_document->findPart(m_partIds.front());
        if (nullptr != m_part)
            m_partId = m_partIds.front();
    }
}

QColor ComponentPropertyWidget::lastColor()
{
    QColor color = Qt::white;
    std::map<QString, int> colorMap;
    for (const auto& partId : m_partIds) {
        const Document::Part* part = m_document->findPart(partId);
        if (nullptr == part)
            continue;
        colorMap[part->color.name()]++;
    }
    if (!colorMap.empty()) {
        color = std::max_element(colorMap.begin(), colorMap.end(),
            [](const std::map<QString, int>::value_type& a, const std::map<QString, int>::value_type& b) {
                return a.second < b.second;
            })->first;
    }
    return color;
}

dust3d::Uuid ComponentPropertyWidget::lastColorImageId()
{
    dust3d::Uuid colorImageId;
    std::map<dust3d::Uuid, int> colorImageIdMap;
    for (const auto& partId : m_partIds) {
        const Document::Part* part = m_document->findPart(partId);
        if (nullptr == part)
            continue;
        if (part->colorImageId.isNull())
            continue;
        colorImageIdMap[part->colorImageId]++;
    }
    if (!colorImageIdMap.empty()) {
        colorImageId = std::max_element(colorImageIdMap.begin(), colorImageIdMap.end(),
            [](const std::map<dust3d::Uuid, int>::value_type& a, const std::map<dust3d::Uuid, int>::value_type& b) {
                return a.second < b.second;
            })->first;
    }
    return colorImageId;
}

void ComponentPropertyWidget::showColorDialog()
{
    emit beginColorPicking();
    QColor color = QColorDialog::getColor(m_color, this);
    emit endColorPicking();
    if (!color.isValid())
        return;

    for (const auto& partId : m_partIds) {
        emit setPartColorState(partId, true, color);
    }
    emit groupOperationAdded();
}
