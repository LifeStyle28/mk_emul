#pragma once

#include "core/ClockConfig.h"

#include <QObject>
#include <QStringList>
#include <QVariantMap>

class ProjectModel;

// The Clock Configuration dialog (Build menu): a ClockConfig edited from QML, its frequencies
// and limit checks, the live clocks of the emulated MCU for comparison, and the generated
// SystemClock_Config() that can be written into the project.
class ClockModel : public QObject {
    Q_OBJECT
    // Settings (CubeMX names): sources 0 = HSI, 1 = HSE, 2 = PLLCLK.
    Q_PROPERTY(int pllSource READ pllSource WRITE setPllSource NOTIFY changed)
    Q_PROPERTY(int sysclkSource READ sysclkSource WRITE setSysclkSource NOTIFY changed)
    Q_PROPERTY(int pllM READ pllM WRITE setPllM NOTIFY changed)
    Q_PROPERTY(int pllN READ pllN WRITE setPllN NOTIFY changed)
    Q_PROPERTY(int pllP READ pllP WRITE setPllP NOTIFY changed)
    Q_PROPERTY(int pllQ READ pllQ WRITE setPllQ NOTIFY changed)
    Q_PROPERTY(int ahbDiv READ ahbDiv WRITE setAhbDiv NOTIFY changed)
    Q_PROPERTY(int apb1Div READ apb1Div WRITE setApb1Div NOTIFY changed)
    Q_PROPERTY(int apb2Div READ apb2Div WRITE setApb2Div NOTIFY changed)
    // Results in Hz, problems, generated code.
    Q_PROPERTY(QVariantMap freq READ freq NOTIFY changed)
    Q_PROPERTY(QStringList errors READ errors NOTIFY changed)
    Q_PROPERTY(QStringList warnings READ warnings NOTIFY changed)
    Q_PROPERTY(QStringList bad READ bad NOTIFY changed)
    Q_PROPERTY(int flashLatency READ flashLatency NOTIFY changed)
    Q_PROPERTY(bool halProject READ halProject NOTIFY changed)
    Q_PROPERTY(QString code READ code NOTIFY changed)
    // What the firmware in the emulator configured (RCC registers), in Hz.
    Q_PROPERTY(QVariantMap live READ live NOTIFY liveChanged)
public:
    explicit ClockModel(ProjectModel *project, QObject *parent = nullptr);

    int pllSource() const { return cfg_.pllSource; }
    int sysclkSource() const { return cfg_.sysclkSource; }
    int pllM() const { return static_cast<int>(cfg_.pllM); }
    int pllN() const { return static_cast<int>(cfg_.pllN); }
    int pllP() const { return static_cast<int>(cfg_.pllP); }
    int pllQ() const { return static_cast<int>(cfg_.pllQ); }
    int ahbDiv() const { return static_cast<int>(cfg_.ahbDiv); }
    int apb1Div() const { return static_cast<int>(cfg_.apb1Div); }
    int apb2Div() const { return static_cast<int>(cfg_.apb2Div); }
    void setPllSource(int v);
    void setSysclkSource(int v);
    void setPllM(int v);
    void setPllN(int v);
    void setPllP(int v);
    void setPllQ(int v);
    void setAhbDiv(int v);
    void setApb1Div(int v);
    void setApb2Div(int v);

    QVariantMap freq() const;
    QStringList errors() const;
    QStringList warnings() const;
    QStringList bad() const;
    int flashLatency() const { return result_.flashLatency; }
    bool halProject() const;
    QString code() const;
    QVariantMap live() const { return live_; }

    // From the machine snapshot (EmulatorController).
    void setLive(uint32_t pllcfgr, uint32_t cfgr, uint32_t sysclk, uint32_t hclk, uint32_t pclk1, uint32_t pclk2,
                 uint32_t tim1, uint32_t tim2);

    Q_INVOKABLE void loadFromEmulator();
    Q_INVOKABLE void preset(const QString &name); // "168", "84", "hsi16", "hse8"
    Q_INVOKABLE void copyCode();
    // Replaces SystemClock_Config() in the project, or adds clock_config.c / .h; returns what
    // was done, for the dialog.
    Q_INVOKABLE QString writeToProject();

signals:
    void changed();
    void liveChanged();

private:
    void update();
    void save() const;
    void loadForProject();

    ProjectModel *project_;
    ClockConfig cfg_;
    ClockConfig::Result result_;
    ClockConfig liveCfg_;
    QVariantMap live_;
    uint32_t livePllcfgr_ = 0;
    uint32_t liveCfgr_ = 0;
};
