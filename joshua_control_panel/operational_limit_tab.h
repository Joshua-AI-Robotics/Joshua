#ifndef OPERATIONAL_LIMIT_TAB_H
#define OPERATIONAL_LIMIT_TAB_H

#include <QWidget>
#include <memory>
#include <QMap>

class QLabel;

namespace Ui { class OperationalLimitTab; }

class OperationalLimitTab : public QWidget {
	Q_OBJECT
public:
	explicit OperationalLimitTab(QWidget* parent = nullptr);
	~OperationalLimitTab();

signals:
	void readingUpdated(float min_value, float max_value);
	void readingUpdatedForTopic(QString topic, float min_value, float max_value);
	void logMessage(const QString& message);

private slots:
	void onLogMessage(const QString& message);

	void on_start_subscribe_Button_clicked();
	void on_stop_subscribe_Button_clicked();
	void on_update_to_existing_preset_Button_clicked();
	void on_save_as_raw_text_Button_clicked();
	
	void onReadingUpdated(float min_value, float max_value);
	void onReadingUpdatedForTopic(QString topic, float min_value, float max_value);

private:
	class RosSubscriberRunner; // defined in .cc

	Ui::OperationalLimitTab* ui;
	std::unique_ptr<RosSubscriberRunner> subscriberRunner_;
	QMap<QString, QPair<QLabel*, QLabel*>> topicToMinMaxLabels_;
	QMap<QString, QLabel*> topicToTopicLabel_;
};

#endif // OPERATIONAL_LIMIT_TAB_H 