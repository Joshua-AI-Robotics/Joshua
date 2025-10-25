#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <QtCore/QTimer>
#include <QtGui/QCloseEvent>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QVBoxLayout>
#include <memory>

// Forward declarations
class QTabWidget;
class QWidget;
class GeneralTab;
class ConfigTab;
class MonitorTab;
class CalibrationTab;

class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow();

 protected:
  void closeEvent(QCloseEvent* event) override;

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

  // Tabs
  QTabWidget* tabWidget_;
  GeneralTab* generalTab_;
  ConfigTab* configTab_;
  MonitorTab* monitorTab_;
  CalibrationTab* calibrationTab_;

  // Layout components
  QSplitter* mainSplitter_;

  // Timer for periodic updates
  QTimer* updateTimer_;

  // Menu actions
  QAction* aboutAction_;
  QAction* preferencesAction_;
  QAction* exitAction_;
};

#endif  // MAIN_WINDOW_H
