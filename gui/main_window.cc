#include "main_window.h"
#include "general_tab.h"
#include "config_tab.h"
#include "monitor_tab.h"
#include <QtWidgets/QApplication>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QMessageBox>
#include <QtCore/QSettings>
#include <QtCore/QDir>
#include <QtCore/QStandardPaths>
#include <QtWidgets/QStyle>
#include <QtGui/QScreen>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QLabel>
#include <QtWidgets/QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , updateTimer_(new QTimer(this))
{
    setupUI();
    setupMenuBar();
    setupStatusBar();
    setupCentralWidget();
    connectSignals();
    
    // Set window properties
    setWindowTitle("Project Joshua - Robot Control Interface");
    setMinimumSize(1200, 800);
    
    // Center window on screen
    QScreen *screen = QApplication::primaryScreen();
    QRect screenGeometry = screen->geometry();
    int x = (screenGeometry.width() - width()) / 2;
    int y = (screenGeometry.height() - height()) / 2;
    move(x, y);
    
    // Start update timer (30 FPS)
    updateTimer_->start(33); // ~30 FPS
}

MainWindow::~MainWindow() = default;

void MainWindow::setupUI() {
    // Create tabs
    tabWidget_ = new QTabWidget(this);
    generalTab_ = new GeneralTab(this);
    configTab_ = new ConfigTab(this);
    monitorTab_ = new MonitorTab(this);
}

void MainWindow::setupMenuBar() {
    QMenuBar *menuBar = this->menuBar();
    
    // File menu
    QMenu *fileMenu = menuBar->addMenu("&File");
    exitAction_ = fileMenu->addAction("E&xit");
    exitAction_->setShortcut(QKeySequence::Quit);
    connect(exitAction_, &QAction::triggered, this, &QWidget::close);
    
    // Tools menu
    QMenu *toolsMenu = menuBar->addMenu("&Tools");
    preferencesAction_ = toolsMenu->addAction("&Preferences...");
    connect(preferencesAction_, &QAction::triggered, this, &MainWindow::onPreferences);
    
    // Help menu
    QMenu *helpMenu = menuBar->addMenu("&Help");
    aboutAction_ = helpMenu->addAction("&About");
    connect(aboutAction_, &QAction::triggered, this, &MainWindow::onAbout);
}

void MainWindow::setupStatusBar() {
    QStatusBar *statusBar = this->statusBar();
    statusBar->showMessage("Ready");
}

void MainWindow::setupCentralWidget() {
    // Add tabs to tab widget
    tabWidget_->addTab(generalTab_, "General");
    tabWidget_->addTab(configTab_, "Config");
    tabWidget_->addTab(monitorTab_, "Monitor");

    // Set as central widget
    setCentralWidget(tabWidget_);
}

void MainWindow::connectSignals() {
    // Connections are handled within individual tabs where appropriate
}

void MainWindow::onUpdateTimer() {
    // Forward periodic updates to the General tab (if needed)
    if (generalTab_) {
        generalTab_->updateStatus();
    }
}

void MainWindow::onAbout() {
    QMessageBox::about(this, "About Project Joshua",
        "<h3>Project Joshua - Robot Control Interface</h3>"
        "<p>Version 0.1.0</p>"
        "<p>A modern Qt6-based GUI for controlling and monitoring "
        "the Project Joshua robot system.</p>"
        "<p>Built with Qt6 and C++</p>");
}

void MainWindow::onPreferences() {
    // TODO: Implement preferences dialog
    QMessageBox::information(this, "Preferences",
        "Preferences dialog not yet implemented.");
}

void MainWindow::closeEvent(QCloseEvent *event) {
    // Save window geometry
    QSettings settings;
    settings.setValue("geometry", saveGeometry());
    settings.setValue("windowState", saveState());
    
    // Accept the close event
    event->accept();
} 