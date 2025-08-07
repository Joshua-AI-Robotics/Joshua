#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <QtWidgets/QMainWindow>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QStatusBar>
#include <QtCore/QTimer>
#include <QtGui/QCloseEvent>
#include <memory>

// Forward declarations
class ControlPanel;
class StatusPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onUpdateTimer();
    void onAbout();
    void onPreferences();

private:
    void setupUI();
    void setupMenuBar();
    void setupStatusBar();
    void setupCentralWidget();
    void connectSignals();

    // UI Components
    std::unique_ptr<ControlPanel> controlPanel_;
    std::unique_ptr<StatusPanel> statusPanel_;
    
    // Layout components
    QSplitter* mainSplitter_;
    
    // Timer for periodic updates
    QTimer* updateTimer_;
    
    // Menu actions
    QAction* aboutAction_;
    QAction* preferencesAction_;
    QAction* exitAction_;
};

#endif // MAIN_WINDOW_H 