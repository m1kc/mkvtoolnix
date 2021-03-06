#include "common/common_pch.h"

#include "common/sorting.h"
#include "mkvtoolnix-gui/source_file_model.h"
#include "mkvtoolnix-gui/track_model.h"

#include <QFileInfo>

SourceFileModel::SourceFileModel(QObject *parent)
  : QAbstractItemModel{parent}
  , m_sourceFiles{}
  , m_tracksModel{}
{
  m_additionalPartIcon.addFile(":/icons/16x16/distribute-horizontal-margin.png");
  m_addedIcon.addFile(":/icons/16x16/distribute-horizontal-x.png");
  m_normalIcon.addFile(":/icons/16x16/distribute-vertical-page.png");
}

SourceFileModel::~SourceFileModel() {
}

void
SourceFileModel::setTracksModel(TrackModel *tracksModel) {
  m_tracksModel = tracksModel;
}

void
SourceFileModel::setSourceFiles(QList<SourceFilePtr> &sourceFiles) {
  m_sourceFiles = &sourceFiles;
  reset();
}

QModelIndex
SourceFileModel::index(int row,
                       int column,
                       QModelIndex const &parent)
  const {
  if (!m_sourceFiles || (0 > row) || (0 > column))
    return QModelIndex{};

  auto parentFile = sourceFileFromIndex(parent);
  if (!parentFile)
    return row < m_sourceFiles->size() ? createIndex(row, column, m_sourceFiles->at(row).get()) : QModelIndex{};

  auto childFile = row <  parentFile->m_additionalParts.size()                                       ? parentFile->m_additionalParts[row].get()
                 : row < (parentFile->m_additionalParts.size() + parentFile->m_appendedFiles.size()) ? parentFile->m_appendedFiles[row - parentFile->m_additionalParts.size()].get()
                 :                                                                                     nullptr;

  if (!childFile)
    return QModelIndex{};

  return createIndex(row, column, childFile);
}

QModelIndex
SourceFileModel::parent(QModelIndex const &child)
  const {
  auto sourceFile = sourceFileFromIndex(child);
  if (!sourceFile)
    return QModelIndex{};

  auto parentSourceFile = sourceFile->m_appendedTo;
  if (!parentSourceFile)
    return QModelIndex{};

  auto grandparentSourceFile = parentSourceFile->m_appendedTo;
  if (!grandparentSourceFile) {
    for (int row = 0; m_sourceFiles->size() > row; ++row)
      if (m_sourceFiles->at(row).get() == parentSourceFile)
        return createIndex(row, 0, parentSourceFile);
    return QModelIndex{};
  }

  for (int row = 0; grandparentSourceFile->m_additionalParts.size() > row; ++row)
    if (grandparentSourceFile->m_additionalParts[row].get() == parentSourceFile)
      return createIndex(row, 0, parentSourceFile);

  for (int row = 0; grandparentSourceFile->m_appendedFiles.size() > row; ++row)
    if (grandparentSourceFile->m_appendedFiles[row].get() == parentSourceFile)
      return createIndex(row + parentSourceFile->m_additionalParts.size(), 0, parentSourceFile);

  return QModelIndex{};
}

int
SourceFileModel::rowCount(QModelIndex const &parent)
  const {
  if (parent.column() > 0)
    return 0;
  if (!m_sourceFiles)
    return 0;
  auto parentSourceFile = sourceFileFromIndex(parent);
  if (!parentSourceFile)
    return m_sourceFiles->size();
  return parentSourceFile->m_additionalParts.size() + parentSourceFile->m_appendedFiles.size();
}

int
SourceFileModel::columnCount(QModelIndex const &)
  const {
  return NumberOfColumns;
}

QVariant
SourceFileModel::dataDecoration(QModelIndex const &index,
                                SourceFile *sourceFile)
  const {
  return FileNameColumn != index.column() ? QVariant{}
       : sourceFile->m_additionalPart     ? m_additionalPartIcon
       : sourceFile->m_appended           ? m_addedIcon
       :                                    m_normalIcon;
}

QVariant
SourceFileModel::dataDisplay(QModelIndex const &index,
                             SourceFile *sourceFile)
  const {
  QFileInfo info{sourceFile->m_fileName};
  if (FileNameColumn == index.column())
    return QFileInfo{sourceFile->m_fileName}.fileName();

  else if (ContainerColumn == index.column())
    return sourceFile->m_additionalPart ? QY("(additional part)") : sourceFile->m_container;

  else if (SizeColumn == index.column())
    return QFileInfo{sourceFile->m_fileName}.size();

  else if (DirectoryColumn == index.column())
    return QFileInfo{sourceFile->m_fileName}.filePath();

  else
    return QVariant{};
}

QVariant
SourceFileModel::data(QModelIndex const &index,
                      int role)
  const {
  if (role == Qt::TextAlignmentRole)
    return SizeColumn == index.column() ? Qt::AlignRight : Qt::AlignLeft;

  auto sourceFile = sourceFileFromIndex(index);
  if (!sourceFile)
    return QVariant{};

  if (role == Qt::DecorationRole)
    return dataDecoration(index, sourceFile);

  if (role == Qt::DisplayRole)
    return dataDisplay(index, sourceFile);

  return QVariant{};
}

QVariant
SourceFileModel::headerData(int section,
                            Qt::Orientation orientation,
                            int role)
  const {
  if (Qt::Horizontal != orientation)
    return QVariant{};

  if (Qt::DisplayRole == role)
    return FileNameColumn  == section ? QY("File name")
         : ContainerColumn == section ? QY("Container")
         : SizeColumn      == section ? QY("File size")
         : DirectoryColumn == section ? QY("Directory")
         :                               Q("INTERNAL ERROR");

  if (Qt::TextAlignmentRole == role)
    return SizeColumn == section ? Qt::AlignRight : Qt::AlignLeft;

  return QVariant{};
}

SourceFile *
SourceFileModel::sourceFileFromIndex(QModelIndex const &index)
  const {
  if (index.isValid())
    return static_cast<SourceFile *>(index.internalPointer());
  return nullptr;
}

void
SourceFileModel::addAdditionalParts(QModelIndex fileToAddToIdx,
                                    QStringList fileNames) {
  if (fileNames.isEmpty() || !fileToAddToIdx.isValid())
    return;

  if (sourceFileFromIndex(fileToAddToIdx)->m_additionalPart)
    fileToAddToIdx = parent(fileToAddToIdx);

  auto fileToAddTo = sourceFileFromIndex(fileToAddToIdx);
  assert(fileToAddToIdx.isValid() && fileToAddTo);

  fileNames.erase(std::remove_if(fileNames.begin(), fileNames.end(), [&](QString const &fileName) {
        if (fileToAddTo->m_fileName == fileName)
          return true;
        for (auto additionalPart : fileToAddTo->m_additionalParts)
          if (additionalPart->m_fileName == fileName)
            return true;
        return false;
      }), fileNames.end());

  if (fileNames.isEmpty())
    return;

  mtx::sort::naturally(fileNames);

  auto startRow = fileToAddTo->m_additionalParts.size();
  auto endRow   = startRow + fileNames.size() - 1;

  beginInsertRows(fileToAddToIdx, startRow, endRow);

  for (auto &fileName : fileNames) {
    auto additionalPart              = std::make_shared<SourceFile>(fileName);
    additionalPart->m_additionalPart = true;
    additionalPart->m_appendedTo     = fileToAddTo;

    fileToAddTo->m_additionalParts << additionalPart;
  }

  endInsertRows();
}

void
SourceFileModel::addOrAppendFilesAndTracks(QModelIndex fileToAddToIdx,
                                           QList<SourceFilePtr> const &files,
                                           bool append) {
  assert(m_tracksModel);

  if (files.isEmpty())
    return;

  if (append)
    appendFilesAndTracks(fileToAddToIdx, files);
  else
    addFilesAndTracks(files);
}

void
SourceFileModel::addFilesAndTracks(QList<SourceFilePtr> const &files) {
  beginInsertRows(QModelIndex{}, m_sourceFiles->size(), m_sourceFiles->size() + files.size() - 1);
  *m_sourceFiles << files;
  endInsertRows();

  m_tracksModel->addTracks(std::accumulate(files.begin(), files.end(), QList<TrackPtr>{}, [](QList<TrackPtr> &accu, SourceFilePtr const &file) { return accu << file->m_tracks; }));
}

void
SourceFileModel::appendFilesAndTracks(QModelIndex fileToAddToIdx,
                                      QList<SourceFilePtr> const &files) {
  assert(false);
  if (!fileToAddToIdx.isValid())
    return;

  // auto fileToAddTo = sourceFileFromIndex(fileToAddToIdx);
    // if (fileToAddTo->isAdditionalPart() || fileToAddTo->isAppended())
    //   fileToAddToIdx = parent(fileToAddToIdx);
}

//   if (append) {
//   } else
//     fileToAddToIdx

//   fileToAddTo = sourceFileFromIndex(fileToAddToIdx);
//   assert(fileToAddToIdx.isValid() && fileToAddTo);

//   *m_sourceFiles << identifier.file();
//   for (auto &track : identifier.file()->m_tracks)
//     m_config.m_tracks << track.get();
// }

void
SourceFileModel::clear() {
  if (m_sourceFiles->isEmpty())
    return;

  beginResetModel();
  m_sourceFiles->clear();
  endResetModel();
}
