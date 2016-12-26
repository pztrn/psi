/*
 * chatviewtheme.cpp - theme for webkit based chatview
 * Copyright (C) 2010 Rion (Sergey Ilinyh)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

#ifdef QT_WEBENGINEWIDGETS_LIB
#include <QWebEnginePage>
#include <QWebChannel>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineProfile>
#include <functional>
#else
#include <QWebPage>
#include <QWebFrame>
#endif
#include <QFileInfo>
#include <QApplication>
#include <QScopedPointer>
#include <time.h>
#include <tuple>

#include "chatviewtheme.h"
#include "psioptions.h"
#include "coloropt.h"
#include "jsutil.h"
#include "psiwkavatarhandler.h"
#include "webview.h"
#include "chatviewthemeprovider.h"
#include "themeserver.h"

#ifndef QT_WEBENGINEWIDGETS_LIB
# ifdef HAVE_QT5
#  define QT_JS_OWNERSHIP QWebFrame::QtOwnership
# else
#  define QT_JS_OWNERSHIP QScriptEngine::QtOwnership
# endif
#endif


class ChatViewThemePrivate : public QSharedData
{
public:
	ChatViewThemePrivate() : prepareSessionHtml(false) {}

	QString html;
	QString httpRelPath;
	QScopedPointer<ChatViewJSLoader> jsLoader;
	QScopedPointer<ChatViewThemeJSUtil> jsUtil;// it's abslutely the same object for every theme.
	QPointer<WebView> wv;
	bool prepareSessionHtml; // if html should be generated by JS for each session.
	bool transparentBackground;

#ifdef QT_WEBENGINEWIDGETS_LIB
	QList<QWebEngineScript> scripts;
#else
	QStringList scripts;
#endif
	std::function<void(bool)> loadCallback;
};

//class ChatViewJSGlobal
//{

//};

class ChatViewJSLoader : public QObject
{
	Q_OBJECT

	ChatViewTheme *theme;
	QString _loadError;
	QHash<QString, QObject*> _sessions;

	Q_PROPERTY(QString themeId READ themeId CONSTANT)
	Q_PROPERTY(QString isMuc READ isMuc CONSTANT)
	Q_PROPERTY(QString serverUrl READ serverUrl CONSTANT)

signals:
	void sessionHtmlReady(const QString &sessionId, const QString &html);

public:
	ChatViewJSLoader(ChatViewTheme *theme, QObject *parent = 0) :
	    QObject(parent),
	    theme(theme)
	{}

	const QString themeId() const
	{
		return theme->id();
	}

	bool isMuc() const
	{
		return theme->isMuc();
	}

	QString serverUrl() const
	{
		ChatViewThemeProvider *provider = static_cast<ChatViewThemeProvider*>(theme->themeProvider());
		auto server = provider->themeServer();
		QUrl url = server->serverUrl();
		return url.url();
	}

	void registerSession(const QSharedPointer<ChatViewThemeSession> &session)
	{
		_sessions.insert(session->sessionId(), session->jsBridge());
	}

	void unregisterSession(const QString &sessId)
	{
		_sessions.remove(sessId);
	}

public slots:
	void setMetaData(const QVariantMap &map)
	{
		if (map["name"].isValid()) {
			theme->setName(map["name"].toString());
		}
	}

	void finishThemeLoading()
	{
		theme->cvtd->loadCallback(true);
	}

	void errorThemeLoading(const QString &error)
	{
		_loadError = error;
		theme->cvtd->loadCallback(false);
	}

	void setHtml(const QString &h)
	{
		theme->cvtd->html = h;
	}

	void setHttpResourcePath(const QString &relPath)
	{
		theme->cvtd->httpRelPath = relPath;
	}


	// we don't need not text cache since binary data(images?)
	// most likely will be cached by webkit itself
	void toCache(const QString &name, const QVariant &data)
	{
		theme->putToCache(name, data);
	}

	/**
	 * @brief loads content to cache
	 * @param map(cache_key => file in theme)
	 */
	void saveFilesToCache(const QVariantMap &map)
	{
		auto it = map.constBegin();
		while (it != map.constEnd()) {
			QByteArray ba = theme->loadData(it.value().toString());
			if (!ba.isNull()) {
				theme->putToCache(it.key(), ba);
			}
			++it;
		}
	}

	QVariantMap sessionProperties(const QString &sessionId, const QVariantList &props)
	{
		auto sess = _sessions.value(sessionId);
		QVariantMap ret;
		if (sess) {
			for (auto &p : props) {
				QString key = p.toString();
				ret.insert(key, sess->property(key.toUtf8().data()));
			}
		}
		return ret;
	}

	void setDefaultAvatar(const QString &filename, const QString &host = QString())
	{
		QByteArray ba = theme->loadData(filename);
#ifndef QT_WEBENGINEWIDGETS_LIB
		if (!ba.isEmpty()) {
			((PsiWKAvatarHandler *)NetworkAccessManager::instance()
				->schemeHandler("avatar").data())->setDefaultAvatar(ba, host);
		}
#endif
	}

	void setAvatarSize(int width, int height)
	{
#ifndef QT_WEBENGINEWIDGETS_LIB
		((PsiWKAvatarHandler *)NetworkAccessManager::instance()
				->schemeHandler("avatar").data())->setAvatarSize(QSize(width,
																	   height));
#endif
	}

	void setCaseInsensitiveFS(bool state = true)
	{
		theme->setCaseInsensitiveFS(state);
	}

	void setPrepareSessionHtml(bool enabled = true)
	{
		theme->cvtd->prepareSessionHtml = enabled;
	}

	void setSessionHtml(const QString &sessionId, const QString &html)
	{
		emit sessionHtmlReady(sessionId, html);
	}

	QString getFileContents(const QString &name) const
	{
		return QString(theme->loadData(name));
	}

	QString getFileContentsFromAdapterDir(const QString &name) const
	{
		QString adapterPath = theme->themeProvider()->themePath(QLatin1String("chatview/") + theme->id().split('/').first());
		QFile file(adapterPath + "/" + name);
		if (file.open(QIODevice::ReadOnly)) {
			QByteArray result = file.readAll();
			file.close();
			return QString::fromUtf8(result.constData(), result.size());
		} else {
			qDebug("Failed to open file %s: %s", qPrintable(file.fileName()),
				   qPrintable(file.errorString()));
		}
		return QString();
	}

	void setTransparent()
	{
		theme->setTransparentBackground(true);
	}
};







// JS Bridge object emedded by theme. Has any logic unrelted to contact itself
class ChatViewThemeJSUtil : public QObject {
	Q_OBJECT

	QString _jsNs; // in js: window.<_jsNs> - is where we keep all our objects. theme loader can change it.
	QMap<QString,QVariant> _cache;

	Q_PROPERTY(QString jsNamespace READ jsNamespace WRITE setJSNamespace NOTIFY jsNamespaceChanged)

signals:
	void jsNamespaceChanged();

public:
	ChatViewThemeJSUtil(QObject *parent = 0) :
	    QObject(parent),
	    _jsNs(QLatin1String("psi"))
	{
	}

	const QString jsNamespace() const
	{
		return _jsNs;
	}

	void setJSNamespace(const QString &ns)
	{
		_jsNs = ns;
	}

	inline void putToCache(const QString &key, const QVariant &data)
	{
		_cache.insert(key, data);
	}

public slots:
	QVariantMap loadFromCacheMulti(const QVariantList &list)
	{
		QVariantMap ret;
		for (auto &item : list) {
			QString key = item.toString();
			ret[key] = _cache.value(key);
		}
		return ret;
	}

	QVariant cache(const QString &name) const
	{
		return _cache.value(name);
	}


	QString psiOption(const QString &option) const
	{
		return JSUtil::variant2js(PsiOptions::instance()->getOption(option));
	}

	QString colorOption(const QString &option) const
	{
		return JSUtil::variant2js(ColorOpt::instance()->color(option));
	}

	QString formatDate(const QDateTime &dt, const QString &format) const
	{
		return dt.toLocalTime().toString(format);
	}

	QString strftime(const QDateTime &dt, const QString &format) const
	{
		char str[256];
		time_t t = dt.toTime_t();
		int s = ::strftime(str, 256, format.toLocal8Bit(), localtime(&t));
		if (s) {
			return QString::fromLocal8Bit(str, s);
		}
		return QString();
	}

	void console(const QString &text) const
	{
		qDebug("%s", qPrintable(text));
	}

	QString status2text(int status) const
	{
		return ::status2txt(status);
	}

	QString hex2rgba(const QString &hex, float opacity)
	{
		QColor color("#" + hex);
		color.setAlphaF(opacity);
		return QString("rgba(%1,%2,%3,%4)").arg(color.red()).arg(color.green())
				.arg(color.blue()).arg(color.alpha());
	}
};


//------------------------------------------------------------------------------
// ChatViewTheme
//------------------------------------------------------------------------------
ChatViewTheme::ChatViewTheme()
{

}

ChatViewTheme::ChatViewTheme(ChatViewThemeProvider *provider) :
	Theme(provider),
    cvtd(new ChatViewThemePrivate())
{
}

ChatViewTheme::ChatViewTheme(const ChatViewTheme &other) :
    Theme(other),
    cvtd(other.cvtd)
{

}

ChatViewTheme &ChatViewTheme::operator=(const ChatViewTheme &other)
{
	Theme::operator=(other);
	cvtd = other.cvtd;
	return *this;
}

ChatViewTheme::~ChatViewTheme()
{
}

/**
 * @brief Sets theme bridge, starts loading procedure from javascript adapter.
 * @param file full path to theme directory
 * @param helperScripts adapter.js and util.js
 * @param adapterPath path to directry with adapter
 * @return true on success
 */
bool ChatViewTheme::load(const QString &id, std::function<void(bool)> loadCallback)
{
	ChatViewThemeProvider *provider = static_cast<ChatViewThemeProvider*>(themeProvider());
	QString tp = provider->themePath(QLatin1String("chatview/") + id);
	if (id.isEmpty() || tp.isEmpty()) {
		return false;
	}

	setFilePath(tp);
	setId(id);
	cvtd->loadCallback = loadCallback;

	QStringList idParts = id.split('/');
	QString themeType, themeId;
	std::tie(themeType, themeId) = std::tie(idParts[0], idParts[1]);
	QStringList scriptPaths = QStringList()
	        << provider->themePath(QLatin1String("chatview/util.js"))
	        << provider->themePath(QLatin1String("chatview/") + themeType + QLatin1String("/adapter.js"));

	if (cvtd->jsUtil.isNull())
		cvtd->jsLoader.reset(new ChatViewJSLoader(this));
		cvtd->jsUtil.reset(new ChatViewThemeJSUtil());
		//cvtd->jsUtil->setJSNamespace(QLatin1String("psi")); // set by constructor
	if (cvtd->wv.isNull()) {
		cvtd->wv = new WebView(0);
		//d->wv->setHtml("", baseUrl());
	}

	//d->loadCallback = callback;


#if QT_WEBENGINEWIDGETS_LIB
	QWebChannel * channel = new QWebChannel(cvtd->wv->page());
	cvtd->wv->page()->setWebChannel(channel);
	channel->registerObject(QLatin1String("srvLoader"), cvtd->jsLoader.data());
	channel->registerObject(QLatin1String("srvUtil"), cvtd->jsUtil.data());

	QWebEngineScriptCollection &sc = cvtd->wv->page()->scripts();
	for (auto &sp : scriptPaths) {
		QFile f(sp);
		if (f.open(QIODevice::ReadOnly)) {
			QWebEngineScript script;
			script.setSourceCode(f.readAll());
			script.setInjectionPoint(QWebEngineScript::DocumentCreation);
			script.setWorldId(QWebEngineScript::MainWorld);
			cvtd->scripts.append(script);
			qDebug() << "Adding: " << sp;
			sc.insert(script);
		}
	}

	//QString themeServer = ChatViewThemeProvider::serverAddr();
	cvtd->wv->page()->setHtml(QString(
	    "<html><head>"
	    "<script src=\"qrc:///qtwebchannel/qwebchannel.js\"></script>"
		"<script type=\"text/javascript\">\n"
			"document.addEventListener(\"DOMContentLoaded\", function () {\n"
				"new QWebChannel(qt.webChannelTransport, function (channel) {\n"
					"window.srvLoader = channel.objects.srvLoader;\n"
					"window.srvUtil = channel.objects.srvUtil;\n"
	                "initPsiTheme().adapter.loadTheme();\n"
				"});\n"
			"});\n"
		"</script></head></html>")
	);
	return true;
#else
	d->wv->page()->mainFrame()->addToJavaScriptWindowObject("srvLoader", d->jsLoader, QT_JS_OWNERSHIP);
	d->wv->page()->mainFrame()->addToJavaScriptWindowObject("srvUtil", d->jsUtil, QT_JS_OWNERSHIP);

	foreach (const QString &sp, scriptPaths) {
		QVariant result = d->wv->page()->mainFrame()->evaluateJavaScript(script);
		if (result != "ok") {
			qDebug("failed to eval helper script: %s...", qPrintable(script.mid(0, 50)));
			return false;
		}
	}

	QString resStr = d->wv->page()->mainFrame()->evaluateJavaScript(
				"try { initPsiTheme().adapter.loadTheme(); } "
				"catch(e) { window[srvUtil.jsNamespace()].util.props(e); }").toString();

	if (resStr == "ok") {
		return true;
	}
	qWarning("javascript part of the theme loader "
			 "didn't return expected result: %s", qPrintable(resStr));
	return false;
#endif
}

bool ChatViewTheme::isMuc() const
{
	return dynamic_cast<GroupChatViewThemeProvider*>(themeProvider());
}

QByteArray ChatViewTheme::screenshot()
{
	return loadData("screenshot.png");
}
#if 0
QString ChatViewTheme::html(QObject *session)
{
	if (d->html.isEmpty()) {
#warning "Remove session from theme. That's at least weird"
#if 0
		if (session) {
			d->wv->page()->mainFrame()->addToJavaScriptWindowObject("chatSession", session);
		}
#endif
		return d->wv->evaluateJS(
					"try { "+d->jsNamespace+".adapter.getHtml(); } catch(e) { e.toString() + ' line:' +e.line; }").toString();
	}
	return d->html;
}
#endif
QString ChatViewTheme::jsNamespace()
{
	return cvtd->jsUtil->jsNamespace();
}

void ChatViewTheme::putToCache(const QString &key, const QVariant &data)
{
	cvtd->jsUtil->putToCache(key, data);
}

void ChatViewTheme::setTransparentBackground(bool enabled)
{
	cvtd->transparentBackground = enabled;
}

bool ChatViewTheme::isTransparentBackground() const
{
	return cvtd->transparentBackground;
}

bool ChatViewTheme::applyToWebView(QSharedPointer<ChatViewThemeSession> session)
{
#if QT_WEBENGINEWIDGETS_LIB
	QWebEnginePage *page = session->webView()->page();
	if (cvtd->transparentBackground) {
		page->setBackgroundColor(Qt::transparent);
	}
#else
	QWebPage *page = session->webView()->page();
	if (cvtd->transparentBackground) {
		QPalette palette;
		palette = session->webView()->palette();
		palette.setBrush(QPalette::Base, Qt::transparent);
		page->setPalette(palette);
		session->webView()->setAttribute(Qt::WA_OpaquePaintEvent, false);
	}
#endif

#if QT_WEBENGINEWIDGETS_LIB


	Q_ASSERT(page->webChannel() == NULL); // It does not make much sense to reinit
	// from doc: Note: A current limitation is that objects must be registered before any client is initialized.

	QWebChannel *channel = new QWebChannel(cvtd->wv->page());
	channel->registerObject(QLatin1String("srvUtil"), cvtd->jsUtil.data());
	channel->registerObject(QLatin1String("srvSession"), session->jsBridge());
	page->setWebChannel(channel);

	page->scripts().insert(cvtd->scripts);

	ChatViewThemeProvider *provider = static_cast<ChatViewThemeProvider*>(themeProvider());
	page->profile()->setRequestInterceptor(provider->requestInterceptor());

	auto server = provider->themeServer();
	session->server = server;
	session->theme = *this;
	auto weakSession = session.toWeakRef();
	auto handler = [weakSession](qhttp::server::QHttpRequest* req, qhttp::server::QHttpResponse* res) -> bool
	{
		auto session = weakSession.lock();
		if (!session) {
			return false;
		}
		auto pair = session->getContents(req->url());
		if (pair.first.isNull()) {
			// not handled by chat. try handle by theme
			QString path = req->url().path(); // fully decoded
			if (path.isEmpty() || path == QLatin1String("/")) {
				res->setStatusCode(qhttp::ESTATUS_OK);
				res->headers().insert("Content-Type", "text/html;charset=utf-8");


				if (session->theme.cvtd->prepareSessionHtml) { // html should be prepared for ech individual session
					// Even crazier stuff starts here.
					// Basically we send to theme's webview instance a signal to
					// generate html for specific session. It does it async.
					// Then javascript sends a signal the html is ready,
					// indicating sessionId and html contents.
					// And only then we close the request with hot html.

					session->theme.cvtd->jsLoader->connect(session->theme.cvtd->jsLoader.data(),
					                        &ChatViewJSLoader::sessionHtmlReady,
					                        session->jsBridge(),
					[session, res](const QString &sessionId, const QString &html)
					{
						if (session->sessId == sessionId) {
							res->end(html.toUtf8()); // return html to client
							// and disconnect from loader
							session->theme.cvtd->jsLoader->disconnect(
							            session->theme.cvtd->jsLoader.data(),
							            &ChatViewJSLoader::sessionHtmlReady,
							            session->jsBridge(), nullptr);
							session->theme.cvtd->jsLoader->unregisterSession(session->sessId);
						}
					});
					session->theme.cvtd->jsLoader->registerSession(session);
					session->theme.cvtd->wv->page()->runJavaScript(session->theme.jsNamespace() +
					            QString(QLatin1String(".adapter.generateSessionHtml(\"%1\")"))
					            .arg(session->sessId));

				} else {
					res->end(session->theme.cvtd->html.toUtf8());
				}
				return true;
			} else {
				QByteArray data = session->theme.loadData(session->theme.cvtd->httpRelPath + path);
				if (!data.isNull()) {
					res->setStatusCode(qhttp::ESTATUS_OK);
					res->end(data);
					return true;
				}
			}
			return false;
		}
		res->setStatusCode(qhttp::ESTATUS_OK);
		res->headers().insert("Content-Type", pair.second);
		res->end(pair.first);
		return true;
		// there is a chance the windows is closed already when we come here..
	};
	session->sessId = server->registerHandler(handler);
	QUrl url = server->serverUrl();
	QUrlQuery q;
	q.addQueryItem(QLatin1String("psiId"), session->sessId);
	url.setQuery(q);

	page->load(url);

	//QString id = provider->themeServer()->registerHandler(sessionObject);
	return true;
#else
	connect(chatWv->page()->mainFrame(),
			SIGNAL(javaScriptWindowObjectCleared()), SLOT(embedJsObject()));
#endif
//	QString html = theme->html(jsObject);
//	//qDebug() << "Set html:" << html;
//	webView->page()->mainFrame()->setHtml(
//		html, theme->baseUrl()
//	);
}

ChatViewThemeSession::~ChatViewThemeSession()
{
   if (server) {
	   server->unregisterHandler(sessId);
   }
}

#include "chatviewtheme.moc"

