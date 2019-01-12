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

#include "djsession.h"
#include "dj.h"
#include "track.h"

#if QT_VERSION >= 0x050000
#include <QtConcurrent/QtConcurrent>
#else
#include <QtConcurrentRun>
#endif
#include <QtXml>

struct DjSessionPrivate {
    QMutex mutex1;
    Dj* currentDj;
    int minCount;
    CollectionDB* database;
    QList<Track*> playList1_Tracks;
    QList<Track*> playList2_Tracks;
    QPair<int, int> playList1_Info;
    QPair<int, int> playList2_Info;
    QStringList seenUrls;
    bool isEnabledAutoDJCount;
};

DjSession::DjSession()
    : p(new DjSessionPrivate)
{
    p->database = new CollectionDB();
    p->minCount = 10;
    p->currentDj = nullptr;
    p->isEnabledAutoDJCount = false;
}

DjSession::~DjSession()
{
    delete p;
}

void DjSession::setCurrentDj(Dj* dj)
{
    // retire current Auto-DJ
    if (p->currentDj)
        disconnect(p->currentDj, SIGNAL(filterChanged(Filter*)),
            this, SLOT(on_dj_filterChanged(Filter*)));
    // hire new DJ
    p->currentDj = dj;
    connect(p->currentDj, SIGNAL(filterChanged(Filter*)),
        this, SLOT(on_dj_filterChanged(Filter*)));
}

Dj* DjSession::currentDj()
{
    return p->currentDj;
}

void DjSession::searchTracks()
{
    // init qrand
    QTime time = QTime::currentTime();
    qsrand((uint)time.msec());

    // how many tracks are needed
    p->mutex1.lock();
    int diffCount1 = p->minCount - p->playList1_Tracks.count();
    int diffCount2 = p->minCount - p->playList2_Tracks.count();
    int needed = (diffCount1 > 0) ? diffCount1 : 0;
    needed = (diffCount2 > 0) ? needed + diffCount2 : needed;

    qDebug() << Q_FUNC_INFO << " need " << diffCount1 << " tracks left and " << diffCount2 << " tracks right ";
    qDebug() << Q_FUNC_INFO << " needed together: " << needed;

    // retrieve new random tracks for both playlists
    QList<Track*> tracks1;
    QList<Track*> tracks2;
    for (int i = 0; i < needed; i++) {
        if (i % 2 == 0) {
            if (diffCount1 > 0) {
                tracks1.append(getRandomTrack());
                diffCount1--;
            } else
                tracks2.append(getRandomTrack());
        } else {
            if (diffCount2 > 0) {
                tracks2.append(getRandomTrack());
                diffCount2--;
            } else
                tracks1.append(getRandomTrack());
        }
    }

    // new tracks available, trigger fill up of playlists
    qDebug() << Q_FUNC_INFO << " provide " << tracks1.count() << " tracks left and " << tracks2.count() << " tracks right ";

    // emit if needed
    if (tracks1.count() > 0)
        emit foundTracks_Playlist1(tracks1);
    if (tracks2.count() > 0)
        emit foundTracks_Playlist2(tracks2);

    p->mutex1.unlock();
}

/*ToDo:
- Genre: Combobox with distinct all genres
  */
Track* DjSession::getRandomTrack()
{
    if (p->currentDj == nullptr)
        return nullptr;

    Track* track = nullptr;
    int i = 0;

    Filter* f = p->currentDj->requestFilter();
    int maxCount = 0;

    do {

        track = new Track(p->database->getRandomEntry(f->path(), f->genre(), f->artist()));

        if (maxCount == 0)
            maxCount = p->database->lastMaxCount();
        i++;
    } while ((track->prettyLength() == "?"
                 || track->containIn(p->playList1_Tracks)
                 || track->containIn(p->playList2_Tracks)
                 || p->seenUrls.contains(track->url().toString()))
        && i < maxCount * 3);
    if (i >= maxCount * 3)
        qDebug() << Q_FUNC_INFO << " no new track found.";
    else
        qDebug() << Q_FUNC_INFO << i << " iterations to found a new track " << i;

    f->setCount(maxCount);
    f->setLength(p->database->lastLengthSum());
    p->seenUrls.append(track->url().toString());
    p->seenUrls.removeDuplicates();

    track->setFlags(track->flags() | Track::isAutoDjSelection);
    return track;
}

void DjSession::updatePlaylists()
{
    QFuture<void> future = QtConcurrent::run(this, &DjSession::searchTracks);
}

void DjSession::forceTracks(QList<Track*> tracks)
{
    int count1 = p->playList1_Tracks.count();
    int count2 = p->playList2_Tracks.count();

    qDebug() << Q_FUNC_INFO << count1 << " tracks left and " << count2 << " tracks right ";

    // distribute tracks to both playlists
    QList<Track*> tracks1;
    QList<Track*> tracks2;
    for (int i = 0; i < tracks.count(); i++) {

        if (tracks.at(i)->flags().testFlag(Track::isOnFirstPlayer)) {
            tracks1.append(tracks.at(i));
            count1++;
        } else if (tracks.at(i)->flags().testFlag(Track::isOnSecondPlayer)) {

            tracks2.append(tracks.at(i));
            count2++;
        } else if (count1 < count2) {

            tracks1.append(tracks.at(i));
            count1++;
        } else {
            tracks2.append(tracks.at(i));
            count2++;
        }
    }

    // new tracks available, trigger fill up of playlists
    qDebug() << Q_FUNC_INFO << " provide " << tracks1.count() << " tracks left and " << tracks2.count() << " tracks right ";

    // emit if needed
    if (tracks1.count() > 0)
        emit foundTracks_Playlist1(tracks1);
    if (tracks2.count() > 0)
        emit foundTracks_Playlist2(tracks2);
}

void DjSession::playDefaultList()
{
    qDebug() << Q_FUNC_INFO;

    QList<QStringList> selectedTags;

    //Retrieve songs from database
    selectedTags = p->database->selectPlaylistTracks("defaultKnowthelist");

    QList<Track*> tracks;

    tracks.clear();

    qDebug() << Q_FUNC_INFO << "Song count: " << selectedTags.count();

    //add tags to this track list
    foreach (QStringList tag, selectedTags) {
        tracks.append(new Track(tag));
    }

    forceTracks(tracks);
}

void DjSession::on_dj_filterChanged(Filter* f)
{
    qDebug() << Q_FUNC_INFO;
    int cnt = p->database->getCount(f->path(), f->genre(), f->artist());
    f->setLength(p->database->lastLengthSum());
    f->setCount(cnt);
    summariseCount();
}

void DjSession::onResetStats()
{
    p->database->resetSongCounter();
}

void DjSession::onTrackFinished(Track* track)
{
    if (track && (p->isEnabledAutoDJCount || !track->flags().testFlag(Track::isAutoDjSelection))) {
        p->database->incSongCounter(track->url().toLocalFile());
    }
}

void DjSession::onTrackPropertyChanged(Track* track)
{
    if (track) {
        p->database->setSongRate(track->url().toLocalFile(), track->rate());
    }
}

void DjSession::onTracksChanged_Playlist1(QList<Track*> tracks)
{
    p->playList1_Info.second = 0;
    p->playList1_Tracks = tracks;
    foreach (Track* track, p->playList1_Tracks) {
        Track::Options flags = track->flags();
        flags |= Track::isOnFirstPlayer;
        flags &= ~Track::isOnSecondPlayer;
        track->setFlags(flags);
        p->playList1_Info.second += track->length();
    }
    p->playList1_Info.first = p->playList1_Tracks.count();
    Q_EMIT changed_Playlist1(p->playList1_Info);
}

void DjSession::onTracksChanged_Playlist2(QList<Track*> tracks)
{
    p->playList2_Info.second = 0;
    p->playList2_Tracks = tracks;
    foreach (Track* track, p->playList2_Tracks) {
        Track::Options flags = track->flags();
        flags &= ~Track::isOnFirstPlayer;
        flags |= Track::isOnSecondPlayer;
        track->setFlags(flags);
        p->playList2_Info.second += track->length();
    }
    p->playList2_Info.first = p->playList2_Tracks.count();
    Q_EMIT changed_Playlist2(p->playList2_Info);
}

void DjSession::storePlaylists(const QString& name, bool replace)
{
    qDebug() << Q_FUNC_INFO << " Start";

    QList<Track*> listToStore;
    listToStore.append(p->playList1_Tracks);
    listToStore.append(p->playList2_Tracks);

    if (replace)
        p->database->executeSql(QString("DELETE FROM playlists WHERE name ='%1';")
                                    .arg(p->database->escapeString(name)));

    int n = 0;
    QList<Track*>::Iterator i = listToStore.begin();
    while (i != listToStore.end()) {

        QString command = QString("INSERT OR REPLACE INTO playlists "
                                  "( url, name, length, flags, norder, changedate ) "
                                  "VALUES('%1','%2', %3, %4, %5, strftime('%s', 'now'));")
                              .arg(p->database->escapeString((*i)->url().toLocalFile()))
                              .arg(p->database->escapeString(name))
                              .arg((*i)->length())
                              .arg((*i)->flags())
                              .arg(n);

        p->database->executeSql(command);
        i++;
        n++;
    }

    Q_EMIT savedPlaylists();
    qDebug() << Q_FUNC_INFO << " Insert finish";
}

void DjSession::summariseCount()
{
    QString res;
    int count = 0;
    int length = 0;
    QStringList artists;
    int filterCount = p->currentDj->filters().count();
    for (int i = 0; i < filterCount; i++) {
        Filter* f = p->currentDj->filters().at(i);
        res += f->description();
        count += f->count();
        length += f->length();
    }
    p->currentDj->setLengthTracks(length);
    p->currentDj->setDescription(res);
    p->currentDj->setCountTracks(count);
}

bool DjSession::isEnabledAutoDJCount()
{
    return p->isEnabledAutoDJCount;
}

void DjSession::setIsEnabledAutoDJCount(bool value)
{
    p->isEnabledAutoDJCount = value;
}

int DjSession::minCount()
{
    return p->minCount;
}

void DjSession::setMinCount(int value)
{
    p->minCount = value;
}

// obsolate: keep this just in case we need an export function later
void DjSession::savePlaylists(const QString& filename)
{
    qDebug() << Q_FUNC_INFO << "BEGIN ";
    QFile file(filename);

    if (!file.open(QFile::WriteOnly))
        return;

    QList<Track*> listToSave;
    listToSave.append(p->playList1_Tracks);
    listToSave.append(p->playList2_Tracks);

    QDomDocument newdoc;
    QDomElement playlistElem = newdoc.createElement("playlist");
    playlistElem.setAttribute("version", "1");
    playlistElem.setAttribute("xmlns", "http://xspf.org/ns/0/");
    newdoc.appendChild(playlistElem);

    QDomElement elem = newdoc.createElement("creator");
    QDomText t = newdoc.createTextNode("Knowthelist");
    elem.appendChild(t);
    playlistElem.appendChild(elem);

    QDomElement listElem = newdoc.createElement("trackList");

    QList<Track*>::Iterator i = listToSave.begin();
    while (i != listToSave.end()) {

        QDomElement trackElem = newdoc.createElement("track");

        QDomElement extElem = newdoc.createElement("extension");

        if ((*i)->flags().testFlag(Track::isAutoDjSelection))
            extElem.setAttribute("isAutoDjSelection", "1");
        if ((*i)->flags().testFlag(Track::isOnFirstPlayer))
            extElem.setAttribute("isOnFirstPlayer", "1");
        if ((*i)->flags().testFlag(Track::isOnSecondPlayer))
            extElem.setAttribute("isOnSecondPlayer", "1");
        extElem.setAttribute("Rating", (*i)->rate());

        QStringList tag = (*i)->tagList();

        for (int x = 0; x < tag.count(); ++x) {
            if (x == 4 || x == 5) {
                extElem.setAttribute(Track::tagNameList.at(x), tag.at(x));
            } else {
                QDomElement elem = newdoc.createElement(Track::tagNameList.at(x));
                QDomText t = newdoc.createTextNode(tag.at(x));
                elem.appendChild(t);
                trackElem.appendChild(elem);
            }
        }

        trackElem.appendChild(extElem);
        listElem.appendChild(trackElem);

        i++;
    }

    playlistElem.appendChild(listElem);

    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    stream << "<?xml version=\"1.0\" encoding=\"utf8\"?>\n";
    stream << newdoc.toString();
    file.close();

    Q_EMIT savedPlaylists();
    qDebug() << Q_FUNC_INFO << "END ";
}
