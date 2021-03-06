/**************************************************************************
* Otter Browser: Web browser controlled by the user, not vice-versa.
* Copyright (C) 2013 - 2014 Michal Dutkiewicz aka Emdek <michal@emdek.pl>
* Copyright (C) 2014 Jan Bajer aka bajasoft <jbajer@gmail.com>
* Copyright (C) 2014 Piotr Wójcik <chocimier@tlen.pl>
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
**************************************************************************/

#include "AddressWidget.h"
#include "BookmarkPropertiesDialog.h"
#include "ContentsWidget.h"
#include "Window.h"
#include "../core/AddressCompletionModel.h"
#include "../core/BookmarksManager.h"
#include "../core/BookmarksModel.h"
#include "../core/SearchesManager.h"
#include "../core/SettingsManager.h"
#include "../core/Utils.h"

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QRegularExpression>
#include <QtCore/QStandardPaths>
#include <QtCore/QTimer>
#include <QtGui/QClipboard>
#include <QtGui/QContextMenuEvent>
#include <QtGui/QPainter>
#include <QtWidgets/QApplication>
#include <QtWidgets/QMenu>
#include <QtWidgets/QStyleOptionFrame>

namespace Otter
{

AddressWidget::AddressWidget(Window *window, bool simpleMode, QWidget *parent) : QLineEdit(parent),
	m_window(NULL),
	m_completer(new QCompleter(AddressCompletionModel::getInstance(), this)),
	m_bookmarkLabel(NULL),
	m_loadPluginsLabel(NULL),
	m_urlIconLabel(NULL),
	m_lookupIdentifier(0),
	m_lookupTimer(0),
	m_simpleMode(simpleMode)
{
	m_completer->setCaseSensitivity(Qt::CaseInsensitive);
	m_completer->setCompletionMode(QCompleter::InlineCompletion);
	m_completer->setCompletionRole(Qt::DisplayRole);
	m_completer->setFilterMode(Qt::MatchStartsWith);

	setWindow(window);
	setCompleter(m_completer);
	setMinimumWidth(100);
	installEventFilter(this);

	if (!m_simpleMode)
	{
		optionChanged(QLatin1String("AddressField/ShowBookmarkIcon"), SettingsManager::getValue(QLatin1String("AddressField/ShowBookmarkIcon")));
		optionChanged(QLatin1String("AddressField/ShowUrlIcon"), SettingsManager::getValue(QLatin1String("AddressField/ShowUrlIcon")));
		setPlaceholderText(tr("Enter address or search..."));
		setMouseTracking(true);

		connect(SettingsManager::getInstance(), SIGNAL(valueChanged(QString,QVariant)), this, SLOT(optionChanged(QString,QVariant)));
	}

	connect(this, SIGNAL(textChanged(QString)), this, SLOT(setCompletion(QString)));
	connect(this, SIGNAL(returnPressed()), this, SLOT(notifyRequestedLoadUrl()));
	connect(BookmarksManager::getInstance(), SIGNAL(modelModified()), this, SLOT(updateBookmark()));
}

void AddressWidget::timerEvent(QTimerEvent *event)
{
	if (event->timerId() == m_lookupTimer)
	{
		QHostInfo::abortHostLookup(m_lookupIdentifier);

		killTimer(m_lookupTimer);

		m_lookupTimer = 0;

		emit requestedSearch(m_lookupQuery, SettingsManager::getValue(QLatin1String("Search/DefaultSearchEngine")).toString(), m_hints);

		m_lookupQuery.clear();
	}
}

void AddressWidget::paintEvent(QPaintEvent *event)
{
	QLineEdit::paintEvent(event);

	if (m_simpleMode)
	{
		return;
	}

	QColor badgeColor = QColor(245, 245, 245);
	QStyleOptionFrame panel;

	initStyleOption(&panel);

	panel.palette = palette();
	panel.palette.setColor(QPalette::Base, badgeColor);
	panel.state = QStyle::State_Active;

	QRect rectangle = style()->subElementRect(QStyle::SE_LineEditContents, &panel, this);
	rectangle.setWidth(30);
	rectangle.moveTo(panel.lineWidth, panel.lineWidth);

	m_securityBadgeRectangle = rectangle;

	QPainter painter(this);
	painter.fillRect(rectangle, badgeColor);
	painter.setClipRect(rectangle);

	style()->drawPrimitive(QStyle::PE_PanelLineEdit, &panel, &painter, this);

	QPalette linePalette = palette();
	linePalette.setCurrentColorGroup(QPalette::Disabled);

	painter.setPen(QPen(linePalette.mid().color(), 1));
	painter.drawLine(rectangle.right(), rectangle.top(), rectangle.right(), rectangle.bottom());
}

void AddressWidget::resizeEvent(QResizeEvent *event)
{
	QLineEdit::resizeEvent(event);

	updateIcons();
}

void AddressWidget::focusInEvent(QFocusEvent *event)
{
	if (event->reason() == Qt::MouseFocusReason && childAt(mapFromGlobal(QCursor::pos())))
	{
		return;
	}

	QLineEdit::focusInEvent(event);

	if (!text().trimmed().isEmpty() && (event->reason() == Qt::MouseFocusReason || event->reason() == Qt::ShortcutFocusReason || (!m_simpleMode && (event->reason() == Qt::TabFocusReason || event->reason() == Qt::BacktabFocusReason))) && SettingsManager::getValue(QLatin1String("AddressField/SelectAllOnFocus")).toBool())
	{
		QTimer::singleShot(0, this, SLOT(selectAll()));
	}
	else
	{
		deselect();
	}
}

void AddressWidget::contextMenuEvent(QContextMenuEvent *event)
{
	QMenu *menu = createStandardContextMenu();

	if (!m_simpleMode)
	{
		const QString shortcut = QKeySequence(QKeySequence::Paste).toString(QKeySequence::NativeText);
		bool found = false;

		if (!shortcut.isEmpty())
		{
			for (int i = 0; i < menu->actions().count(); ++i)
			{
				if (menu->actions().at(i)->text().endsWith(shortcut))
				{
					menu->insertAction(menu->actions().at(i + 1), ActionsManager::getAction(PasteAndGoAction, this));

					found = true;

					break;
				}
			}
		}

		if (!found)
		{
			menu->insertAction(menu->actions().at(6), ActionsManager::getAction(PasteAndGoAction, this));
		}
	}

	menu->exec(event->globalPos());
	menu->deleteLater();
}

void AddressWidget::mouseMoveEvent(QMouseEvent *event)
{
	QLineEdit::mouseMoveEvent(event);

	if (!m_simpleMode)
	{
		if (m_securityBadgeRectangle.contains(event->pos()))
		{
			setCursor(Qt::ArrowCursor);
		}
		else
		{
			setCursor(Qt::IBeamCursor);
		}
	}
}

void AddressWidget::mouseReleaseEvent(QMouseEvent *event)
{
	if (text().isEmpty() && event->button() == Qt::MiddleButton && !QApplication::clipboard()->text().isEmpty() && SettingsManager::getValue(QLatin1String("AddressField/PasteAndGoOnMiddleClick")).toBool())
	{
		handleUserInput(QApplication::clipboard()->text().trimmed());

		event->accept();
	}
	else
	{
		QLineEdit::mouseReleaseEvent(event);
	}
}

void AddressWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton)
	{
		selectAll();

		event->accept();
	}
	else
	{
		QLineEdit::mouseDoubleClickEvent(event);
	}
}

void AddressWidget::handleUserInput(const QString &text, OpenHints hints)
{
	BookmarksItem *bookmark = BookmarksManager::getBookmark(text);

	if (bookmark)
	{
		emit requestedOpenBookmark(bookmark, hints);

		return;
	}

	if (text == QString(QLatin1Char('~')) || text.startsWith(QLatin1Char('~') + QDir::separator()))
	{
		const QStringList locations = QStandardPaths::standardLocations(QStandardPaths::HomeLocation);

		if (!locations.isEmpty())
		{
			emit requestedOpenUrl(locations.first() + text.mid(1), hints);

			return;
		}
	}

	if (QFileInfo(text).exists())
	{
		emit requestedOpenUrl(QUrl::fromLocalFile(QFileInfo(text).canonicalFilePath()), hints);

		return;
	}

	const QUrl url = QUrl::fromUserInput(text);

	if (url.isValid() && (url.isLocalFile() || QRegularExpression(QLatin1String("^(\\w+\\:\\S+)|([\\w\\-]+\\.[a-zA-Z]{2,}(/\\S*)?$)")).match(text).hasMatch()))
	{
		emit requestedOpenUrl(url, hints);

		return;
	}

	const QString keyword = text.section(QLatin1Char(' '), 0, 0);
	SearchInformation *engine = SearchesManager::getSearchEngine(keyword, true);

	if (engine)
	{
		emit requestedSearch(text.section(QLatin1Char(' '), 1), engine->identifier, hints);

		return;
	}

	if (keyword == QLatin1String("?"))
	{
		emit requestedSearch(text.section(QLatin1Char(' '), 1), QString(), hints);

		return;
	}

	const int lookupTimeout = SettingsManager::getValue(QLatin1String("AddressField/HostLookupTimeout")).toInt();

	if (!m_simpleMode && url.isValid() && lookupTimeout > 0)
	{
		if (text == m_lookupQuery)
		{
			return;
		}

		m_lookupQuery = text;
		m_hints = hints;

		if (m_lookupTimer != 0)
		{
			QHostInfo::abortHostLookup(m_lookupIdentifier);

			killTimer(m_lookupTimer);

			m_lookupTimer = 0;
		}

		m_lookupIdentifier = QHostInfo::lookupHost(url.host(), this, SLOT(verifyLookup(QHostInfo)));
		m_lookupTimer = startTimer(lookupTimeout);

		return;
	}

	emit requestedSearch(text, QString(), hints);
}

void AddressWidget::optionChanged(const QString &option, const QVariant &value)
{
	if (option == QLatin1String("AddressField/ShowBookmarkIcon"))
	{
		if (value.toBool() && !m_bookmarkLabel)
		{
			m_bookmarkLabel = new QLabel(this);
			m_bookmarkLabel->setObjectName(QLatin1String("Bookmark"));
			m_bookmarkLabel->setAutoFillBackground(false);
			m_bookmarkLabel->setFixedSize(16, 16);
			m_bookmarkLabel->setPixmap(Utils::getIcon(QLatin1String("bookmarks")).pixmap(m_bookmarkLabel->size(), QIcon::Disabled));
			m_bookmarkLabel->setCursor(Qt::ArrowCursor);
			m_bookmarkLabel->setFocusPolicy(Qt::NoFocus);
			m_bookmarkLabel->installEventFilter(this);

			updateIcons();
		}
		else if (!value.toBool() && m_bookmarkLabel)
		{
			m_bookmarkLabel->deleteLater();
			m_bookmarkLabel = NULL;

			updateIcons();
		}
	}
	else if (option == QLatin1String("AddressField/ShowUrlIcon"))
	{
		if (value.toBool() && !m_urlIconLabel)
		{
			m_urlIconLabel = new QLabel(this);
			m_urlIconLabel->setObjectName(QLatin1String("Url"));
			m_urlIconLabel->setAutoFillBackground(false);
			m_urlIconLabel->setFixedSize(16, 16);
			m_urlIconLabel->setPixmap((m_window ? m_window->getIcon() : Utils::getIcon(QLatin1String("tab"))).pixmap(m_urlIconLabel->size()));
			m_urlIconLabel->setFocusPolicy(Qt::NoFocus);
			m_urlIconLabel->installEventFilter(this);

			QMargins margins = textMargins();
			margins.setLeft(52);

			setTextMargins(margins);

			if (m_window)
			{
				connect(m_window, SIGNAL(iconChanged(QIcon)), this, SLOT(setIcon(QIcon)));
			}

			updateIcons();
		}
		else
		{
			if (!value.toBool() && m_urlIconLabel)
			{
				m_urlIconLabel->deleteLater();
				m_urlIconLabel = NULL;

				updateIcons();
			}

			QMargins margins = textMargins();
			margins.setLeft(30);

			setTextMargins(margins);

			if (m_window)
			{
				disconnect(m_window, SIGNAL(iconChanged(QIcon)), this, SLOT(setIcon(QIcon)));
			}
		}
	}
	else if (option == QLatin1String("AddressField/ShowLoadPluginsIcon") && m_window)
	{
		if (value.toBool())
		{
			connect(m_window->getContentsWidget()->getAction(LoadPluginsAction), SIGNAL(changed()), this, SLOT(updateLoadPlugins()));
		}
		else
		{
			disconnect(m_window->getContentsWidget()->getAction(LoadPluginsAction), SIGNAL(changed()), this, SLOT(updateLoadPlugins()));
		}

		updateLoadPlugins();
	}
}

void AddressWidget::removeIcon()
{
	QAction *action = qobject_cast<QAction*>(sender());

	if (action)
	{
		SettingsManager::setValue(QStringLiteral("AddressField/Show%1Icon").arg(action->data().toString()), false);
	}
}

void AddressWidget::verifyLookup(const QHostInfo &host)
{
	killTimer(m_lookupTimer);

	m_lookupTimer = 0;

	if (host.error() == QHostInfo::NoError)
	{
		emit requestedOpenUrl(getUrl(), m_hints);
	}
	else
	{
		emit requestedSearch(m_lookupQuery, SettingsManager::getValue(QLatin1String("Search/DefaultSearchEngine")).toString(), m_hints);
	}

	m_lookupQuery.clear();
}

void AddressWidget::notifyRequestedLoadUrl()
{
	const QString input = text().trimmed();

	if (!input.isEmpty())
	{
		handleUserInput(input);
	}
}

void AddressWidget::updateBookmark()
{
	if (!m_bookmarkLabel)
	{
		return;
	}

	const QUrl url = getUrl();

	if (url.scheme() == QLatin1String("about"))
	{
		m_bookmarkLabel->setEnabled(false);
		m_bookmarkLabel->setPixmap(Utils::getIcon(QLatin1String("bookmarks")).pixmap(m_bookmarkLabel->size(), QIcon::Disabled));
		m_bookmarkLabel->setToolTip(QString());

		return;
	}

	const bool hasBookmark = BookmarksManager::hasBookmark(url.toString());

	m_bookmarkLabel->setEnabled(true);
	m_bookmarkLabel->setPixmap(Utils::getIcon(QLatin1String("bookmarks")).pixmap(m_bookmarkLabel->size(), (hasBookmark ? QIcon::Active : QIcon::Disabled)));
	m_bookmarkLabel->setToolTip(hasBookmark ? tr("Remove Bookmark") : tr("Add Bookmark"));
}

void AddressWidget::updateLoadPlugins()
{
	const bool canLoadPlugins = (SettingsManager::getValue(QLatin1String("AddressField/ShowLoadPluginsIcon")).toBool() && m_window && m_window->getContentsWidget()->getAction(LoadPluginsAction)->isEnabled());

	if (canLoadPlugins && !m_loadPluginsLabel)
	{
		m_loadPluginsLabel = new QLabel(this);
		m_loadPluginsLabel->show();
		m_loadPluginsLabel->setObjectName(QLatin1String("LoadPlugins"));
		m_loadPluginsLabel->setAutoFillBackground(false);
		m_loadPluginsLabel->setFixedSize(16, 16);
		m_loadPluginsLabel->setPixmap(Utils::getIcon(QLatin1String("preferences-plugin")).pixmap(m_loadPluginsLabel->size()));
		m_loadPluginsLabel->setCursor(Qt::ArrowCursor);
		m_loadPluginsLabel->setToolTip(tr("Click to load all contents handled by plugins on the page"));
		m_loadPluginsLabel->setFocusPolicy(Qt::NoFocus);
		m_loadPluginsLabel->installEventFilter(this);

		updateIcons();
	}
	else if (!canLoadPlugins && m_loadPluginsLabel)
	{
		m_loadPluginsLabel->deleteLater();
		m_loadPluginsLabel = NULL;

		updateIcons();
	}
}

void AddressWidget::updateIcons()
{
	if (m_bookmarkLabel)
	{
		m_bookmarkLabel->move((width() - 22), ((height() - m_bookmarkLabel->height()) / 2));
	}

	if (m_loadPluginsLabel)
	{
		m_loadPluginsLabel->move((width() - 22 - (m_bookmarkLabel ? 22 : 0)), ((height() - m_loadPluginsLabel->height()) / 2));
	}

	if (m_urlIconLabel)
	{
		m_urlIconLabel->move(36, ((height() - m_urlIconLabel->height()) / 2));
	}
}

void AddressWidget::setCompletion(const QString &text)
{
	m_completer->setCompletionPrefix(text);
}

void AddressWidget::setIcon(const QIcon &icon)
{
	if (m_urlIconLabel)
	{
		m_urlIconLabel->setPixmap(icon.pixmap(m_urlIconLabel->size()));
	}
}

void AddressWidget::setUrl(const QUrl &url)
{
	updateBookmark();

	if (m_window && !hasFocus() && url.scheme() != QLatin1String("javascript"))
	{
		setText(m_window->isUrlEmpty() ? QString() : url.toString());
	}
}

void AddressWidget::setWindow(Window *window)
{
	if (m_window)
	{
		disconnect(m_window, SIGNAL(aboutToClose()), this, SLOT(setWindow()));
		disconnect(m_window, SIGNAL(iconChanged(QIcon)), this, SLOT(setIcon(QIcon)));
		disconnect(m_window->getContentsWidget()->getAction(LoadPluginsAction), SIGNAL(changed()), this, SLOT(updateLoadPlugins()));
	}

	m_window = window;

	if (window)
	{
		if (m_urlIconLabel)
		{
			setIcon(window->getIcon());
			setUrl(window->getUrl());

			connect(window, SIGNAL(iconChanged(QIcon)), this, SLOT(setIcon(QIcon)));
		}

		connect(window, SIGNAL(aboutToClose()), this, SLOT(setWindow()));
		connect(window->getContentsWidget()->getAction(LoadPluginsAction), SIGNAL(changed()), this, SLOT(updateLoadPlugins()));
	}

	updateLoadPlugins();
}

QUrl AddressWidget::getUrl() const
{
	return QUrl(text().isEmpty() ? QLatin1String("about:blank") : text());
}

bool AddressWidget::eventFilter(QObject *object, QEvent *event)
{
	if (object == m_bookmarkLabel && m_bookmarkLabel && m_window && event->type() == QEvent::MouseButtonPress)
	{
		QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);

		if (mouseEvent && mouseEvent->button() == Qt::LeftButton)
		{
			if (m_bookmarkLabel->isEnabled())
			{
				const QString url = getUrl().toString();

				if (BookmarksManager::hasBookmark(url))
				{
					BookmarksManager::deleteBookmark(url);
				}
				else
				{
					BookmarksItem *bookmark = new BookmarksItem(BookmarksItem::UrlBookmark, getUrl().adjusted(QUrl::RemovePassword), m_window->getTitle());
					BookmarkPropertiesDialog dialog(bookmark, NULL, this);

					if (dialog.exec() == QDialog::Rejected)
					{
						delete bookmark;
					}
				}

				updateBookmark();
			}

			event->accept();

			return true;
		}
	}

	if (object == m_loadPluginsLabel && m_loadPluginsLabel && m_window && event->type() == QEvent::MouseButtonPress)
	{
		QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);

		if (mouseEvent && mouseEvent->button() == Qt::LeftButton)
		{
			m_window->getContentsWidget()->triggerAction(LoadPluginsAction);

			event->accept();

			return true;
		}
	}

	if (object != this && event->type() == QEvent::ContextMenu)
	{
		QContextMenuEvent *contextMenuEvent = static_cast<QContextMenuEvent*>(event);

		if (contextMenuEvent)
		{
			QMenu menu(this);
			QAction *action = menu.addAction(tr("Remove This Icon"), this, SLOT(removeIcon()));
			action->setData(object->objectName());

			menu.exec(contextMenuEvent->globalPos());

			event->accept();

			return true;
		}
	}

	if (object == this && event->type() == QEvent::KeyPress && m_window)
	{
		QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);

		if (keyEvent->key() == Qt::Key_Escape)
		{
			const QUrl url = m_window->getUrl();

			if (text().trimmed().isEmpty() || text().trimmed() != url.toString())
			{
				setText(m_window->isUrlEmpty() ? QString() : url.toString());

				if (!text().trimmed().isEmpty() && SettingsManager::getValue(QLatin1String("AddressField/SelectAllOnFocus")).toBool())
				{
					QTimer::singleShot(0, this, SLOT(selectAll()));
				}
			}
			else
			{
				m_window->setFocus();
			}
		}
	}

	if (object == this && event->type() == QEvent::KeyPress)
	{
		QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);

		if ((keyEvent->key() == Qt::Key_Enter || keyEvent->key() == Qt::Key_Return) && keyEvent->modifiers() & Qt::ShiftModifier)
		{
			const QString input = text().trimmed();

			if (!input.isEmpty())
			{
				handleUserInput(input, NewTabOpen);

				return true;
			}
		}
	}

	return QLineEdit::eventFilter(object, event);
}

}
