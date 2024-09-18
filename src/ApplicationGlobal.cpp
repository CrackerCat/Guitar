#include "ApplicationGlobal.h"
#include "MainWindow.h"


void ApplicationGlobal::init(QApplication *a)
{
	(void)a;
	filetype.open();
}

void ApplicationGlobal::writeLog(const std::string_view &str, bool record)
{
	mainwindow->writeLog(str, record);
}

void ApplicationGlobal::writeLog(const QString &str, bool record)
{
	mainwindow->writeLog(str, record);
}

bool ApplicationGlobal::isMainThread()
{
	return QThread::currentThread() == QCoreApplication::instance()->thread();
}
