/*
 * Copyright (C) 2013-2017 Daniel Nicoletti <dantti12@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include "context_p.h"

#include "common.h"
#include "request.h"
#include "response.h"
#include "action.h"
#include "dispatcher.h"
#include "controller.h"
#include "application.h"
#include "stats.h"
#include "enginerequest.h"

#include "config.h"

#include <QUrl>
#include <QUrlQuery>
#include <QCoreApplication>
#include <QBuffer>

using namespace Cutelyst;

Context::Context(ContextPrivate *priv) : d_ptr(priv)
{
}

Context::Context(Application *app) :
    d_ptr(new ContextPrivate(app, app->engine(), app->dispatcher(), app->plugins()))
{
    auto req = new DummyRequest(this);
    req->body = new QBuffer(this);
    req->body->open(QBuffer::ReadWrite);
    req->context = this;

    d_ptr->response = new Response(app->defaultHeaders(), req);
    d_ptr->request = new Request(req);
    d_ptr->request->d_ptr->engine = d_ptr->engine;
}

Context::~Context()
{
    delete d_ptr->request;
    delete d_ptr->response;
    delete d_ptr;
}

bool Context::error() const
{
    Q_D(const Context);
    return !d->error.isEmpty();
}

void Context::error(const QString &error)
{
    Q_D(Context);
    if (error.isEmpty()) {
        d->error.clear();
    } else {
        d->error << error;
        qCCritical(CUTELYST_CORE) << error;
    }
}

QStringList Context::errors() const
{
    Q_D(const Context);
    return d->error;
}

bool Context::state() const
{
    Q_D(const Context);
    return d->state;
}

void Context::setState(bool state)
{
    Q_D(Context);
    d->state = state;
}

Engine *Context::engine() const
{
    Q_D(const Context);
    return d->engine;
}

Application *Context::app() const
{
    Q_D(const Context);
    return d->app;
}

Response *Context::response() const
{
    Q_D(const Context);
    return d->response;
}

Response *Context::res() const
{
    Q_D(const Context);
    return d->response;
}

Action *Context::action() const
{
    Q_D(const Context);
    return d->action;
}

QString Context::actionName() const
{
    Q_D(const Context);
    return d->action->name();
}

QString Context::ns() const
{
    Q_D(const Context);
    return d->action->ns();
}

Request *Context::request() const
{
    Q_D(const Context);
    return d->request;
}

Request *Context::req() const
{
    Q_D(const Context);
    return d->request;
}

Dispatcher *Context::dispatcher() const
{
    Q_D(const Context);
    return d->dispatcher;
}

QString Cutelyst::Context::controllerName() const
{
    Q_D(const Context);
    return QString::fromLatin1(d->action->controller()->metaObject()->className());
}

Controller *Context::controller() const
{
    Q_D(const Context);
    return d->action->controller();
}

Controller *Context::controller(const QString &name) const
{
    Q_D(const Context);
    return d->dispatcher->controllers().value(name);
}

View *Context::customView() const
{
    Q_D(const Context);
    return d->view;
}

View *Context::view(const QString &name) const
{
    Q_D(const Context);
    return d->app->view(name);
}

bool Context::setCustomView(const QString &name)
{
    Q_D(Context);
    d->view = d->app->view(name);
    return d->view;
}

QVariantHash &Context::stash()
{
    Q_D(Context);
    return d->stash;
}

QVariant Context::stash(const QString &key) const
{
    Q_D(const Context);
    return d->stash.value(key);
}

QVariant Context::stash(const QString &key, const QVariant &defaultValue) const
{
    Q_D(const Context);
    return d->stash.value(key, defaultValue);
}

QVariant Context::stashTake(const QString &key)
{
    Q_D(Context);
    return d->stash.take(key);
}

bool Context::stashRemove(const QString &key)
{
    Q_D(Context);
    return d->stash.remove(key);
}

void Context::setStash(const QString &key, const QVariant &value)
{
    Q_D(Context);
    d->stash.insert(key, value);
}

void Context::setStash(const QString &key, const ParamsMultiMap &map)
{
    Q_D(Context);
    d->stash.insert(key, QVariant::fromValue(map));
}

QStack<Component *> Context::stack() const
{
    Q_D(const Context);
    return d->stack;
}

QUrl Context::uriFor(const QString &path, const QStringList &args, const ParamsMultiMap &queryValues) const
{
    Q_D(const Context);

    QUrl uri = d->request->uri();

    QString _path;
    if (path.isEmpty()) {
        // ns must NOT return a leading slash
        const QString controllerNS = d->action->controller()->ns();
        if (!controllerNS.isEmpty()) {
            _path.prepend(controllerNS);
        }
    } else {
        _path = path;
    }

    if (!args.isEmpty()) {
        if (_path == QLatin1String("/")) {
            _path += args.join(QLatin1Char('/'));
        } else {
            _path = _path + QLatin1Char('/') + args.join(QLatin1Char('/'));
        }
    }

    if (!_path.startsWith(QLatin1Char('/'))) {
        _path.prepend(QLatin1Char('/'));
    }
    uri.setPath(_path, QUrl::DecodedMode);

    QUrlQuery query;
    if (!queryValues.isEmpty()) {
        // Avoid a trailing '?'
        if (queryValues.size()) {
            auto it = queryValues.constEnd();
            while (it != queryValues.constBegin()) {
                --it;
                query.addQueryItem(it.key(), it.value());
            }
        }
    }
    uri.setQuery(query);

    return uri;
}

QUrl Context::uriFor(Action *action, const QStringList &captures, const QStringList &args, const ParamsMultiMap &queryValues) const
{
    Q_D(const Context);

    QUrl uri;
    Action *localAction = action;
    if (!localAction) {
        localAction = d->action;
    }

    QStringList localArgs = args;
    QStringList localCaptures = captures;

    Action *expandedAction = d->dispatcher->expandAction(this, action);
    if (expandedAction->numberOfCaptures() > 0) {
        while (localCaptures.size() < expandedAction->numberOfCaptures()
               && localArgs.size()) {
            localCaptures.append(localArgs.takeFirst());
        }
    } else {
        QStringList localCapturesAux = localCaptures;
        localCapturesAux.append(localArgs);
        localArgs = localCapturesAux;
        localCaptures = QStringList();
    }

    const QString path = d->dispatcher->uriForAction(localAction, localCaptures);
    if (path.isEmpty()) {
        qCWarning(CUTELYST_CORE) << "Can not find action for" << localAction << localCaptures;
        return uri;
    }

    uri = uriFor(path, localArgs, queryValues);
    return uri;
}

QUrl Context::uriForAction(const QString &path, const QStringList &captures, const QStringList &args, const ParamsMultiMap &queryValues) const
{
    Q_D(const Context);

    QUrl uri;
    Action *action = d->dispatcher->getActionByPath(path);
    if (!action) {
        qCWarning(CUTELYST_CORE) << "Can not find action for" << path;
        return uri;
    }

    uri = uriFor(action, captures, args, queryValues);
    return uri;
}

bool Context::detached() const
{
    Q_D(const Context);
    return d->detached;
}

void Context::detach(Action *action)
{
    Q_D(Context);
    if (action) {
        d->dispatcher->forward(this, action);
    } else {
        d->detached = true;
    }
}

void Context::detachAsync()
{
    Q_D(Context);
    ++d->asyncDetached;
    d->engineRequest->status |= EngineRequest::Async;
}

void Context::attachAsync()
{
    Q_D(Context);
    if (--d->asyncDetached) {
        return;
    }

    if (Q_UNLIKELY(d->engineRequest->status & EngineRequest::Finalized)) {
        qCWarning(CUTELYST_ASYNC) << "Trying to async attach to a finalized request! Skipping...";
        return;
    }

    while (d->asyncAction < d->pendingAsync.size()) {
        Action *action = d->pendingAsync[d->asyncAction++];
        if (!execute(action)) {
            break; // we are finished
        } else if (d->asyncDetached) {
            return;
        }
    }

    if (d->engineRequest->status & EngineRequest::Async) {
        Q_EMIT d->app->afterDispatch(this);

        finalize();
    }
}

bool Context::forward(Component *action)
{
    Q_D(Context);
    return d->dispatcher->forward(this, action);
}

bool Context::forward(const QString &action)
{
    Q_D(Context);
    return d->dispatcher->forward(this, action);
}

Action *Context::getAction(const QString &action, const QString &ns) const
{
    Q_D(const Context);
    return d->dispatcher->getAction(action, ns);
}

QVector<Action *> Context::getActions(const QString &action, const QString &ns) const
{
    Q_D(const Context);
    return d->dispatcher->getActions(action, ns);
}

QVector<Cutelyst::Plugin *> Context::plugins() const
{
    Q_D(const Context);
    return d->plugins;
}

bool Context::execute(Component *code)
{
    Q_D(Context);
    Q_ASSERT_X(code, "Context::execute", "trying to execute a null Cutelyst::Component");

    static int recursion = qEnvironmentVariableIsSet("RECURSION") ? qEnvironmentVariableIntValue("RECURSION") : 1000;
    if (d->stack.size() >= recursion) {
        QString msg = QStringLiteral("Deep recursion detected (stack size %1) calling %2, %3")
                .arg(QString::number(d->stack.size()), code->reverse(), code->name());
        error(msg);
        setState(false);
        return false;
    }

    bool ret;
    d->stack.push(code);

    if (d->stats) {
        const QString statsInfo = d->statsStartExecute(code);

        ret = code->execute(this);

        // The request might finalize execution before returning
        // so it's wise to check for d->stats again
        if (d->stats && !statsInfo.isEmpty()) {
            d->statsFinishExecute(statsInfo);
        }
    } else {
        ret = code->execute(this);
    }

    d->stack.pop();

    return ret;
}

QLocale Context::locale() const
{
    Q_D(const Context);
    return d->locale;
}

void Context::setLocale(const QLocale &locale)
{
    Q_D(Context);
    d->locale = locale;
}

QVariant Context::config(const QString &key, const QVariant &defaultValue) const
{
    Q_D(const Context);
    return d->app->config(key, defaultValue);
}

QVariantMap Context::config() const
{
    Q_D(const Context);
    return d->app->config();
}

QString Context::translate(const char *context, const char *sourceText, const char *disambiguation, int n) const
{
    Q_D(const Context);
    return d->app->translate(d->locale, context, sourceText, disambiguation, n);
}

bool Context::wait(uint count)
{
    Q_UNUSED(count)
//    Q_D(Context);
//    if (d->loop) {
//        d->loopWait += count;
//        return false;
//    }

//    if (count) {
//        d->loopWait = count;
//        d->loop = new QEventLoop(this);
//        d->loop->exec();
//        return true;
//    }
    return false;
}

void Context::finalize()
{
    Q_D(Context);

    if (Q_UNLIKELY(d->engineRequest->status & EngineRequest::Finalized)) {
        qCWarning(CUTELYST_CORE) << "Trying to finalize a finalized request! Skipping...";
        return;
    }

    if (d->stats) {
        qCDebug(CUTELYST_STATS, "Response Code: %d; Content-Type: %s; Content-Length: %s",
                d->response->status(),
                qPrintable(d->response->headers().header(QStringLiteral("CONTENT_TYPE"), QStringLiteral("unknown"))),
                qPrintable(d->response->headers().header(QStringLiteral("CONTENT_LENGTH"), QStringLiteral("unknown"))));

        const double enlapsed = d->engineRequest->elapsed.nsecsElapsed() / 1000000000.0;
        QString average;
        if (enlapsed == 0.0) {
            average = QStringLiteral("??");
        } else {
            average = QString::number(1.0 / enlapsed, 'f');
            average.truncate(average.size() - 3);
        }
        qCInfo(CUTELYST_STATS) << qPrintable(QStringLiteral("Request took: %1s (%2/s)\n%3")
                                             .arg(QString::number(enlapsed, 'f'),
                                                  average,
                                                  QString::fromLatin1(d->stats->report())));
        delete d->stats;
        d->stats = nullptr;
    }

    d->engineRequest->finalize();
}

void Context::next(bool force)
{
//    Q_D(Context);
    Q_UNUSED(force)
//    if (!d->loop || (--d->loopWait && !force)) {
//        return;
//    }

//    d->loop->quit();
//    d->loop->deleteLater();
//    d->loop = nullptr;
}

QString ContextPrivate::statsStartExecute(Component *code)
{
    QString actionName;
    // Skip internal actions
    if (code->name().startsWith(QLatin1Char('_'))) {
        return actionName;
    }

    actionName = code->reverse();

    if (qobject_cast<Action *>(code)) {
        actionName.prepend(QLatin1Char('/'));
    }

    if (stack.size() > 2) {
        actionName = QLatin1String("-> ") + actionName;
        actionName = actionName.rightJustified(actionName.size() + stack.size() - 2, QLatin1Char(' '));
    }

    stats->profileStart(actionName);

    return actionName;
}

void ContextPrivate::statsFinishExecute(const QString &statsInfo)
{
    stats->profileEnd(statsInfo);
}

void Context::stash(const QVariantHash &unite)
{
    Q_D(Context);
    auto it = unite.constBegin();
    while (it != unite.constEnd()) {
        d->stash.insert(it.key(), it.value());
        ++it;
    }
}

#include "moc_context.cpp"
#include "moc_context_p.cpp"
