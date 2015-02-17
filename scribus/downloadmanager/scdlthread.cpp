
#include "scdlmgr.h"
#include "scdlthread.h"
#include "scpaths.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>


ScDLThread::ScDLThread(QObject *parent) : QThread(parent),
	downloadedCount(0), totalCount(0)
{
	connect(this, SIGNAL(runSignal()), this, SLOT(runSlot()));
}

ScDLThread::~ScDLThread()
{
}

void ScDLThread::run()
{
	emit(runSignal());
}

void ScDLThread::addURL(const QUrl &url, bool overwrite, const QString& location, const QString& destinationLocation)
{
	//qDebug()<<"ScDLThread::addURL:"<<url;
	if (!urlOK(url))
		return;
	QString l(QDir::cleanPath(location));
	if (!l.endsWith("/"))
		l += "/";
	downloadQueue.enqueue(qMakePair(url, l));
	++totalCount;
}

void ScDLThread::addURLs(const QStringList &urlList, bool overwrite, const QString& location, const QString& destinationLocation)
{
	//qDebug()<<"ScDLThread::addURLs:"<<urlList;
	m_urlList=urlList;
	QString l(QDir::cleanPath(location));
	if (!l.endsWith("/"))
		l += "/";
	foreach (QString u, m_urlList)
	{
		QUrl url(u);
		if (!urlOK(u))
			return;
		downloadQueue.enqueue(qMakePair(url, l));
		++totalCount;
	}
}

void ScDLThread::startDownloads()
{
	if (downloadQueue.isEmpty())
	{
		//qDebug()<<"No more downloads left";
		emit finished();
		return;
	}
	start();
}

QString ScDLThread::saveFileName(const QUrl &url, const QString& location, bool overwrite)
{
	QDir d(location);
	QString path = url.path();
	QString basename = QFileInfo(path).fileName();
	if (location.isEmpty() || !d.exists())
	{
		if (basename.isEmpty())
			basename = "download";
		basename = ScPaths::downloadDir() + basename;
	}
	else
	{
		basename = location + basename;
	}
	if (!overwrite && QFile::exists(basename))
	{
		// already exists, don't overwrite
		int i = 0;
		basename += '.';
		while (QFile::exists(basename + QString::number(i)))
			++i;
		basename += QString::number(i);
	}
	return basename;
}

void ScDLThread::startNextDownload()
{
	if (downloadQueue.isEmpty())
	{
		//qDebug()<<downloadedCount<<"/"<<totalCount<<"files downloaded successfully";
		downloadedCount=totalCount=0;
		emit finished();
		return;
	}
	QPair<QUrl, QString> urlPair=downloadQueue.dequeue();

	QString filename = saveFileName(urlPair.first, urlPair.second, true);
	if (filename.isEmpty())
	{
		qDebug()<<"File name empty for url:"<<urlPair.first.toEncoded().constData();
		return;
	}
	output.setFileName(filename);
	if (!output.open(QIODevice::WriteOnly))
	{
		//qDebug()<<"Problem opening save file '"<<qPrintable(filename)<<"' for download '"
		//	   <<urlPair.first.toEncoded().constData()<<"': "<<qPrintable(output.errorString());

		startNextDownload();
		return;
	}
	emit fileStarted(output.fileName());
	QNetworkRequest request(urlPair.first);
	//QNetworkReply *nwr=manager.head(request);
	//qDebug()<<nwr->url()<<nwr->rawHeaderList();
	//connect(&manager, SIGNAL(finished(QNetworkReply*)), this, SLOT(replyFinished(QNetworkReply*)));
	currentDownload = manager.get(request);
	connect(currentDownload, SIGNAL(finished()), this, SLOT(downloadFinished()));
	connect(currentDownload, SIGNAL(readyRead()), this, SLOT(downloadReadyRead()));

	//qDebug()<<"Downloading:"<<urlPair.first.toEncoded().constData();
}

void ScDLThread::downloadFinished()
{
	output.close();
	if (currentDownload->error())
	{
/*
		if(currentDownload->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 301 || currentDownload->rawHeaderList().contains("Location"))
			qDebug()<<currentDownload->header(QNetworkRequest::LocationHeader).toString();
		if(currentDownload->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 302 || currentDownload->rawHeaderList().contains("Location"))
			qDebug()<<currentDownload->header(QNetworkRequest::LocationHeader).toString();
		if(currentDownload->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 307 || currentDownload->rawHeaderList().contains("Location"))
			qDebug()<<currentDownload->header(QNetworkRequest::LocationHeader).toString();

		QUrl redirectUrl = currentDownload->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
		qDebug()<<redirectUrl;
*/
		qDebug()<<"Failed: "<<qPrintable(currentDownload->errorString());
		emit fileFailed(output.fileName());
		if (output.exists())
			output.remove();
	}
	else
	{
		//qDebug()<<"Saving file:"<<qPrintable(output.fileName());
		++downloadedCount;
		emit fileReceived(output.fileName());
	}
	currentDownload->deleteLater();
	startNextDownload();
}

void ScDLThread::replyFinished(QNetworkReply* reply)
{
	qDebug()<<"replyFinished";
	QVariant possibleRedirectUrl =reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
	QUrl urlRedirectedTo = possibleRedirectUrl.toUrl();
	QString text;
	/* If the URL is not empty, we're being redirected. */
	if(!urlRedirectedTo.isEmpty())
	{
		qDebug()<<reply->header(QNetworkRequest::LocationHeader);
		text = QString("QNAMRedirect::replyFinished: Redirected to ").append(urlRedirectedTo.toString());
		addURL(text, true, ScPaths::downloadDir(), ScPaths::downloadDir());
		/* We'll do another request to the redirection url. */
	//	this->_qnam->get(QNetworkRequest(_urlRedirectedTo));
	}
	else
	{
		/*
		 * We weren't redirected anymore
		 * so we arrived to the final destination...
		 */
		text = QString("QNAMRedirect::replyFinished: Arrived to ").append(reply->url().toString());
		urlRedirectedTo.clear();
	}
	qDebug()<<text;
	/* Clean up. */
	reply->deleteLater();
}


void ScDLThread::downloadReadyRead()
{
	output.write(currentDownload->readAll());
}

void ScDLThread::runSlot()
{
	startNextDownload();
}

bool ScDLThread::urlOK(QUrl url)
{
	//TODO: Add some more URL checks
	if (!url.isValid() || url.isEmpty() || url.host().isEmpty())
	{
		//qDebug()<<"URL invalid:"<<url;
		return false;
	}
	return true;
}



