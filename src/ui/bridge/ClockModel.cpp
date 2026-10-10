#include "ui/bridge/ClockModel.h"

#include "ui/bridge/Models.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QSettings>

namespace {

QStringList toQt(const std::vector<std::string> &v) {
    QStringList out;
    for (const auto &s : v)
        out << QString::fromStdString(s);
    return out;
}

} // namespace

ClockModel::ClockModel(ProjectModel *project, QObject *parent)
    : QObject(parent)
    , project_(project) {
    connect(project_, &ProjectModel::projectPathChanged, this, &ClockModel::loadForProject);
    loadForProject();
}

void ClockModel::update() {
    result_ = cfg_.compute();
    save();
    emit changed();
}

// The dialog's settings are kept per project.
void ClockModel::save() const {
    if (project_->projectPath().isEmpty())
        return;
    const QList<QVariant> v{cfg_.pllSource, cfg_.sysclkSource, cfg_.pllM, cfg_.pllN, cfg_.pllP,
                            cfg_.pllQ,      cfg_.ahbDiv,       cfg_.apb1Div, cfg_.apb2Div};
    QSettings().setValue("clock/" + QString(project_->projectPath()).replace('/', '|'), v);
}

void ClockModel::loadForProject() {
    cfg_ = ClockConfig();
    const QVariantList v = QSettings().value("clock/" + QString(project_->projectPath()).replace('/', '|')).toList();
    if (v.size() == 9) {
        cfg_.pllSource = v[0].toInt() == 0 ? ClockConfig::Hsi : ClockConfig::Hse;
        cfg_.sysclkSource = static_cast<ClockConfig::Source>(qBound(0, v[1].toInt(), 2));
        cfg_.pllM = v[2].toUInt();
        cfg_.pllN = v[3].toUInt();
        cfg_.pllP = v[4].toUInt();
        cfg_.pllQ = v[5].toUInt();
        cfg_.ahbDiv = v[6].toUInt();
        cfg_.apb1Div = v[7].toUInt();
        cfg_.apb2Div = v[8].toUInt();
    }
    result_ = cfg_.compute();
    emit changed();
}

void ClockModel::setPllSource(int v) {
    cfg_.pllSource = v == 0 ? ClockConfig::Hsi : ClockConfig::Hse;
    update();
}
void ClockModel::setSysclkSource(int v) {
    cfg_.sysclkSource = static_cast<ClockConfig::Source>(qBound(0, v, 2));
    update();
}
void ClockModel::setPllM(int v) { cfg_.pllM = static_cast<uint32_t>(v); update(); }
void ClockModel::setPllN(int v) { cfg_.pllN = static_cast<uint32_t>(v); update(); }
void ClockModel::setPllP(int v) { cfg_.pllP = static_cast<uint32_t>(v); update(); }
void ClockModel::setPllQ(int v) { cfg_.pllQ = static_cast<uint32_t>(v); update(); }
void ClockModel::setAhbDiv(int v) { cfg_.ahbDiv = static_cast<uint32_t>(v); update(); }
void ClockModel::setApb1Div(int v) { cfg_.apb1Div = static_cast<uint32_t>(v); update(); }
void ClockModel::setApb2Div(int v) { cfg_.apb2Div = static_cast<uint32_t>(v); update(); }

QVariantMap ClockModel::freq() const {
    const auto &r = result_;
    return {{"pllIn", r.pllIn},   {"vcoIn", r.vcoIn},   {"vcoOut", r.vcoOut}, {"pllClk", r.pllClk},
            {"pll48", r.pll48},   {"sysclk", r.sysclk}, {"hclk", r.hclk},     {"pclk1", r.pclk1},
            {"pclk2", r.pclk2},   {"tim1", r.tim1},     {"tim2", r.tim2},     {"pllUsed", r.pllUsed}};
}

QStringList ClockModel::errors() const { return toQt(result_.errors); }
QStringList ClockModel::warnings() const { return toQt(result_.warnings); }
QStringList ClockModel::bad() const { return toQt(result_.bad); }

bool ClockModel::halProject() const {
    return project_->indexOfName("stm32f4xx_hal_conf.h") >= 0 || project_->indexOfName("Core/Inc/stm32f4xx_hal_conf.h") >= 0;
}

QString ClockModel::code() const {
    return QString::fromStdString(halProject() ? cfg_.halCode() : cfg_.registerCode());
}

void ClockModel::setLive(uint32_t pllcfgr, uint32_t cfgr, uint32_t sysclk, uint32_t hclk, uint32_t pclk1,
                         uint32_t pclk2, uint32_t tim1, uint32_t tim2) {
    const QVariantMap m{{"sysclk", sysclk}, {"hclk", hclk}, {"pclk1", pclk1}, {"pclk2", pclk2},
                        {"tim1", tim1},     {"tim2", tim2}};
    livePllcfgr_ = pllcfgr;
    liveCfgr_ = cfgr;
    if (m == live_)
        return;
    live_ = m;
    emit liveChanged();
}

void ClockModel::loadFromEmulator() {
    cfg_ = ClockConfig::fromRegisters(livePllcfgr_, liveCfgr_);
    update();
}

void ClockModel::preset(const QString &name) {
    cfg_ = ClockConfig();
    if (name == "84") { // HSI -> PLL 84 MHz, APB1 42, APB2 84
        cfg_.pllSource = ClockConfig::Hsi;
        cfg_.pllM = 8;
        cfg_.pllN = 168;
        cfg_.pllP = 4;
        cfg_.pllQ = 7;
        cfg_.apb1Div = 2;
        cfg_.apb2Div = 1;
    } else if (name == "hsi16") { // reset state
        cfg_.sysclkSource = ClockConfig::Hsi;
        cfg_.apb1Div = 1;
        cfg_.apb2Div = 1;
    } else if (name == "hse8") {
        cfg_.sysclkSource = ClockConfig::Hse;
        cfg_.apb1Div = 1;
        cfg_.apb2Div = 1;
    }
    update();
}

void ClockModel::copyCode() {
    QGuiApplication::clipboard()->setText(code());
}

QString ClockModel::writeToProject() {
    if (!result_.ok())
        return QStringLiteral("Fix the errors first: the clock tree is out of the STM32F407 limits.");
    if (project_->projectPath().isEmpty())
        return QStringLiteral("Open a project first.");
    const std::string generated = code().toStdString();
    // 1. A file that defines SystemClock_Config() (main.c of CubeMX): replace the function.
    const auto files = project_->files();
    for (int i = 0; i < files.size(); ++i) {
        const QString &name = files[i].first;
        if (!name.endsWith(QLatin1String(".c")))
            continue;
        std::string out;
        if (ClockConfig::replaceFunction(files[i].second.toStdString(), generated, &out)) {
            project_->setFileContent(i, QString::fromStdString(out));
            project_->setCurrentIndex(i);
            return "SystemClock_Config() in " + name + " was replaced. Build & Load, then Run: the clocks above show " +
                   "the emulator's values.";
        }
    }
    // 2. No such function: a separate file; main() has to call it.
    QString src = "#include \"" + QString(halProject() ? "stm32f4xx_hal.h" : "stm32f407xx.h") + "\"\n" +
                  "#include \"clock_config.h\"\n\n" + QString::fromStdString(generated);
    project_->writeFile(QStringLiteral("clock_config.h"),
                        "#pragma once\n\n/* Generated by mk_emul (Build > Clock Configuration). */\n"
                        "void SystemClock_Config(void);\n");
    project_->writeFile(QStringLiteral("clock_config.c"), src);
    return QStringLiteral("Added clock_config.c and clock_config.h. Add #include \"clock_config.h\" and call "
                          "SystemClock_Config() at the start of main(), then Build & Load.");
}
