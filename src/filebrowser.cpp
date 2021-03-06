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

#include "filebrowser.h"
#include <QFileSystemModel>
#include <QTreeView>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QApplication>
#if QT_VERSION >= 0x050000
    #include <QStandardPaths>
#else
    #include <QDesktopServices>
#endif


struct FileBrowserPrivate
{
        QVBoxLayout *layout;
        QTreeView *filetree;
        QFileSystemModel *model;
};

FileBrowser::FileBrowser(QWidget *parent) :
    QWidget(parent)
  , p( new FileBrowserPrivate )
{
     p->layout = new QVBoxLayout;

    // ToDo: maybe we add some buttons and links here

      p->model = new QFileSystemModel;
      p->model->setRootPath(QDir::currentPath());
      p->filetree = new QTreeView(this);
      p->filetree->setModel(p->model);
      p->filetree->setDragEnabled(true);
      p->filetree->setSelectionMode(QAbstractItemView::ContiguousSelection);
      p->filetree->header()->resizeSection(0,400);

#if QT_VERSION >= 0x050000
      p->filetree->setRootIndex(p->model->index(QStandardPaths::standardLocations(QStandardPaths::MusicLocation).at(0)));
#else
      p->filetree->setRootIndex(p->model->index(QDesktopServices::storageLocation(QDesktopServices::MusicLocation)));
#endif


      p->filetree->setAttribute(Qt::WA_MacShowFocusRect, false);
      p->layout->addWidget(p->filetree);

      setLayout(p->layout);
      setAttribute(Qt::WA_MacShowFocusRect, false);
}

void FileBrowser::setRootPath(QString path){

    p->filetree->setRootIndex(p->model->setRootPath(path));

}
