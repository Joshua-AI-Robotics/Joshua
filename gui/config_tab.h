#ifndef CONFIG_TAB_H
#define CONFIG_TAB_H

#include <QtWidgets/QWidget>

namespace Ui { class ConfigTab; }

class ConfigTab : public QWidget {
    Q_OBJECT
public:
    explicit ConfigTab(QWidget* parent = nullptr);
    ~ConfigTab();

private:
    Ui::ConfigTab* ui;
};

#endif // CONFIG_TAB_H 