/*
    Copyright (C) 2005-2014 Mario Stephan <mstephan@shared-files.de>

    This library is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published
    by the Free Software Foundation; either version 2.1 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "collectiontree.h"
#include "collectiontreeitem.h"
#include <QApplication>
#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QtGui>
#if QT_VERSION >= 0x050000
#include <QtConcurrent/QtConcurrent>
#else
#include <QtConcurrentRun>
#endif

struct CollectionTreePrivate {
    CollectionDB* database;
    QList<Track*> tracks;
    QString filterString;
    QMutex mutex;
};

CollectionTree::CollectionTree(QWidget* parent)
    : QTreeWidget(parent)
    , p(new CollectionTreePrivate)
{

    p->database = new CollectionDB();
    p->database->executeSql("PRAGMA synchronous = OFF;");

    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setDragEnabled(true);
    setRootIsDecorated(false);
    setAcceptDrops(false);
    setRootIsDecorated(true);

    setAttribute(Qt::WA_MacShowFocusRect, false);

    QStringList headers;
    headers << tr("Artist");

    QTreeWidgetItem* headeritem = new QTreeWidgetItem(headers);
    headeritem->setTextAlignment(0, Qt::AlignLeft);

    setHeaderItem(headeritem);
    setHeaderLabels(headers);
    header()->resizeSection(0, this->width() - 50);
    header()->setMinimumHeight(18);

    connect(this, SIGNAL(currentItemChanged(QTreeWidgetItem*, QTreeWidgetItem*)),
        this, SLOT(on_currentItemChanged(QTreeWidgetItem*)));

    connect(this, SIGNAL(itemExpanded(QTreeWidgetItem*)),
        this, SLOT(on_itemExpanded(QTreeWidgetItem*)));
}

CollectionTree::~CollectionTree()
{
    p->mutex.tryLock(2000);
    delete p;
}

void CollectionTree::createTrunk()
{
    //qDebug() << Q_FUNC_INFO;
    CollectionTreeItem* item = nullptr;

    clear();

    //add "ALL" node and select it
    int countAll = p->database->getCount();
    if (countAll < 1000 && countAll > 0) {
        item = new CollectionTreeItem(this);
        setCurrentItem(item);
    }

    QList<QStringList> tags;

    //Retrieve root nodes from database
    switch (treeMode) {
    case MODEGENRE:
        tags = p->database->selectGenres();
        if (item)
            item->setGenre(QString::null);
        foreach (QStringList tag, tags) {
            item = new CollectionTreeItem(this);
            item->setGenre(tag[0]);
        }
        headerItem()->setText(0, QString("   %1  (%2)").arg(tr("Genre")).arg(tags.count()));
        break;
        break;
    case MODEYEAR:
        tags = p->database->selectYears();
        if (item)
            item->setYear(QString::null);
        foreach (QStringList tag, tags) {
            item = new CollectionTreeItem(this);
            item->setYear(tag[0]);
        }
        headerItem()->setText(0, QString("   %1  (%2)").arg(tr("Year")).arg(tags.count()));
        break;
        break;
    default:
        tags = p->database->selectArtists();
        if (item)
            item->setArtist(QString::null);
        foreach (QStringList tag, tags) {
            item = new CollectionTreeItem(this);
            item->setArtist(tag[0]);
        }
        headerItem()->setText(0, QString("   %1  (%2)").arg(tr("Artist")).arg(tags.count()));
        break;
    }
}

void CollectionTree::on_itemExpanded(QTreeWidgetItem* item)
{
    qDebug() << Q_FUNC_INFO << endl;
    if (!item)
        return;

    if (item->childCount() == 0) {

        QList<QStringList> tags;
        CollectionTreeItem* collItem = static_cast<CollectionTreeItem*>(item);

        QTreeWidgetItem::ChildIndicatorPolicy policy = QTreeWidgetItem::ShowIndicator;

        //Retrieve children from database
        if (collItem->artist() != QString::null) {
            tags = p->database->selectAlbums(collItem->year(), collItem->genre(), collItem->artist());
            policy = QTreeWidgetItem::DontShowIndicator;
        } else {
            tags = p->database->selectArtists(collItem->year(), collItem->genre());
        }

        foreach (QStringList tag, tags) {
            CollectionTreeItem* child = new CollectionTreeItem(item);

            child->setYear(collItem->year());
            child->setGenre(collItem->genre());
            child->setArtist(collItem->artist());

            if (collItem->artist() == QString::null)
                child->setArtist(tag[0]);
            else
                child->setAlbum(tag[0]);

            child->setChildIndicatorPolicy(policy);
            item->addChild(child);
        }
    }
}

void CollectionTree::triggerRandomSelection()
{
    QFuture<void> future = QtConcurrent::run(this, &CollectionTree::asynchronTriggerRandomSelection);
}

void CollectionTree::asynchronTriggerRandomSelection()
{
    QMutexLocker locker(&p->mutex);
    p->tracks.clear();

    // init qrand
    QTime time = QTime::currentTime();
    qsrand((uint)time.msec());

    for (int i = 0; i < 15; i++) {

        Track* track;
        int r = 0;

        do {
            QStringList tag = p->database->getRandomEntry();
            track = new Track(tag);
            r++;
        } while (track->prettyLength() == "?" && r < 3);

        p->tracks.append(track);
    }

    qDebug() << Q_FUNC_INFO << p->tracks.count();
    emit selectionChanged(p->tracks);
}

void CollectionTree::on_currentItemChanged(QTreeWidgetItem* item)
{
    QFuture<void> future = QtConcurrent::run(this, &CollectionTree::asynchronCurrentItemChanged, item);
}

void CollectionTree::asynchronCurrentItemChanged(QTreeWidgetItem* item)
{
    QMutexLocker locker(&p->mutex);

    if (!item)
        return;

    QList<QStringList> tags;

    CollectionTreeItem* collItem = static_cast<CollectionTreeItem*>(item);
    qDebug() << Q_FUNC_INFO << "Artist: " << collItem->artist() << " Album: " << collItem->album() << endl;

    //Retrieve songs from database
    tags = p->database->selectTracks(collItem->year(), collItem->genre(), collItem->artist(), collItem->album());

    //Show songs in parent's tracklist
    p->tracks.clear();

    qDebug() << Q_FUNC_INFO << "Song count: " << tags.count();

    //add tags to this track list
    foreach (QStringList tag, tags) {
        //qDebug() << Q_FUNC_INFO <<": is playlistitem; tags:"<<tags;
        p->tracks.append(new Track(tag));
    }

    emit selectionChanged(p->tracks);

    //qDebug() << Q_FUNC_INFO << "[End]" << endl;
}

QString CollectionTree::filter()
{
    //ToDo: do we need this private variable
    return p->filterString;
}

void CollectionTree::setFilter(QString filter)
{
    p->filterString = filter;
    p->database->setFilterString(filter);
}

void CollectionTree::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton)
        startPos = e->pos();

    QTreeWidget::mousePressEvent(e);

    if (e->button() == Qt::RightButton)
        showContextMenu(currentItem(), currentColumn());
}

void CollectionTree::mouseMoveEvent(QMouseEvent* event)
{

    if (event->buttons() & Qt::LeftButton) {
        int distance = (event->pos() - startPos).manhattanLength();
        if (distance >= QApplication::startDragDistance()) {
            performDrag();
        }
    }
}

void CollectionTree::performDrag()
{

    QByteArray itemData;
    QDataStream dataStream(&itemData, QIODevice::WriteOnly);
    QVector<QStringList> tags;
    QPixmap cover;
    int i = 0;

    //iterate selected items
    QListIterator<Track*> it(p->tracks);
    while (it.hasNext()) {
        Track* track = dynamic_cast<Track*>(it.next());
        if (track->isValid()) {
            qDebug() << Q_FUNC_INFO << ": send Data:" << track->url();
            QStringList tag = track->tagList();
            tags << tag;
            if (i == 0)
                cover = QPixmap::fromImage(track->coverImage());
            i++;
        }
    }

    dataStream << tags;

    QMimeData* mimeData = new QMimeData;
    mimeData->setData("text/playlistitem", itemData);

    QDrag* drag = new QDrag(this);
    drag->setMimeData(mimeData);

    if (!cover.isNull())
        drag->setPixmap(cover.scaled(50, 50));

    if (drag->exec(Qt::CopyAction | Qt::MoveAction, Qt::CopyAction) == Qt::MoveAction) {
    }
}

void CollectionTree::showContextMenu(QTreeWidgetItem* item, int col)
{

    Q_UNUSED(col);

    //ToDo: create popup before first use
    enum Id { LOAD1,
        LOAD2 };

    if (item == NULL)
        return;

    QMenu popup(this);

    popup.setTitle(item->text(0));
    popup.addAction(style()->standardPixmap(QStyle::SP_MediaPlay), tr("Add to PlayList&1"),
        this, SLOT(onLoad1Triggered()), Qt::Key_1); //, LOAD1
    popup.addAction(style()->standardPixmap(QStyle::SP_MediaPlay), tr("Add to PlayList&2"),
        this, SLOT(onLoad2Triggered()), Qt::Key_2); //, LOAD2
    popup.addSeparator();
    popup.addAction(style()->standardPixmap(QStyle::SP_BrowserReload), tr("Re-scan collection"),
        this, SLOT(onRescanTriggered()), Qt::Key_R);

    popup.exec(QCursor::pos());
}

void CollectionTree::onLoad1Triggered()
{
    Q_EMIT wantLoad(p->tracks, "Left");
}

void CollectionTree::onLoad2Triggered()
{
    Q_EMIT wantLoad(p->tracks, "Right");
}

void CollectionTree::onRescanTriggered()
{
    Q_EMIT rescan();
}

void CollectionTree::showTrackInfo(Track* mb)
{
    const QString body = "<tr><td>%1</td><td>%2</td></tr>";

    QString
        str
        = "<html><body><table STYLE=\"border-collapse: collapse\"> width=\"100%\" border=\"1\">";
    str += body.arg(tr("Title"), mb->title());
    str += body.arg(tr("Artist"), mb->artist());
    str += body.arg(tr("Album"), mb->album());
    str += body.arg(tr("Genre"), mb->genre());
    str += body.arg(tr("Year"), mb->year());
    str += body.arg(tr("Location"), mb->url().toString());
    str += "</table></body></html>";

    QMessageBox::information(0, tr("Meta Information"), str);
}

void CollectionTree::resizeEvent(QResizeEvent*)
{
    //QRect rec = p->collectiontree->geometry();
    header()->resizeSection(0, this->width() - 100);
}
