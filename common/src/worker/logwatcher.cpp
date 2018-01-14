#include "logwatcher.h"

#ifdef Q_OS_WIN
#include "windows.h"
#include "psapi.h"
#endif

static const QStringList START_LINES {"Izaro: Ascend with precision.",
                                      "Izaro: The Goddess is watching.",
                                      "Izaro: Justice will prevail."};
static const QStringList FINISH_LINES {"Izaro: I die for the Empire!",
                                       "Izaro: Delight in your gilded dungeon, ascendant.",
                                       "Izaro: Your destination is more dangerous than the journey, ascendant.",
                                       "Izaro: Triumphant at last!",
                                       "Izaro: You are free!",
                                       "Izaro: The trap of tyranny is inescapable."};
static const QStringList SECTION_FINISH_LINES {"Izaro: By the Goddess! What ambition!",
                                               "Izaro: Such resilience!",
                                               "Izaro: You are inexhaustible!",
                                               "Izaro: You were born for this!"};
static const QStringList LAB_ROOM_PREFIX {"Estate", "Domain", "Basilica", "Mansion", "Sepulchre", "Sanitorium"};
static const QStringList LAB_ROOM_SUFFIX {"Walkways", "Path", "Crossing", "Annex", "Halls", "Passage", "Enclosure", "Atrium"};
static const QRegularExpression LOG_REGEX {"^\\d+/\\d+/\\d+ \\d+:\\d+:\\d+.*?\\[.*?\\] (.*)$"};
static const QRegularExpression ROOM_CHANGE_REGEX {"^: You have entered (.*?)\\.$"};

LogWatcher::LogWatcher(ApplicationModel* model)
{
  this->model = model;
  clientPath = model->get_settings()->value("poeClientPath").toString();
  file.reset(new QFile(QDir(clientPath).filePath("logs/Client.txt")));

  timer.setInterval(1000);
  timer.setSingleShot(false);
  timer.start();
  connect(&timer, &QTimer::timeout,
          this, &LogWatcher::work);
}

void LogWatcher::work()
{
  // reset file if client path settings have changed
  auto newClientPath = model->get_settings()->value("poeClientPath").toString();
  if (clientPath != newClientPath) {
    clientPath = newClientPath;
    file.reset(new QFile(QDir(clientPath).filePath("logs/Client.txt")));
  }

  // attempt to open file
  if (!file->isOpen()) {
    if (!file->open(QIODevice::ReadOnly)) {

      // try to detect client
      clientPath = findGameClientPath();
      if (clientPath.isEmpty()) {
        model->update_logFileOpen(false);
        return;
      }
      file.reset(new QFile(QDir(clientPath).filePath("logs/Client.txt")));
      if (!file->open(QIODevice::ReadOnly)) {
        model->update_logFileOpen(false);
        return;
      }
      model->get_settings()->setValue("poeClientPath", clientPath);
    }
    model->update_logFileOpen(true);
    file->seek(file->size());
  }

  while (true) {
    auto line = file->readLine();
    if (line.isEmpty())
      break;
    parseLine(QString::fromUtf8(line).trimmed());
  }
}

void LogWatcher::parseLine(const QString line)
{
  auto logMatch = LOG_REGEX.match(line);
  if (logMatch.hasMatch()) {
    auto logContent = logMatch.captured(1).trimmed();
    auto roomChangeMatch = ROOM_CHANGE_REGEX.match(logContent);
    if (roomChangeMatch.hasMatch()) {
      auto roomName = roomChangeMatch.captured(1);
      auto affixes = roomName.split(' ');
      if (roomName == "Aspirant\'s Trial" ||
          (affixes.size() == 2 && LAB_ROOM_PREFIX.contains(affixes[0]) && LAB_ROOM_SUFFIX.contains(affixes[1])))
        emit roomChanged(roomName);
      else
        emit labExit();
    } else if (START_LINES.contains(logContent)) {
      emit labStarted();
    } else if (FINISH_LINES.contains(logContent)) {
      emit sectionFinished();
      emit labFinished();
    } else if (SECTION_FINISH_LINES.contains(logContent)) {
      emit sectionFinished();
    }
  }
}

QString LogWatcher::findGameClientPath()
{
#ifdef Q_OS_WIN
  auto hwnd = FindWindowA("POEWindowClass", nullptr);
  if (!hwnd)
    return QString();

  DWORD pid;
  GetWindowThreadProcessId(hwnd, &pid);

  auto handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!handle)
    return QString();

  char buf[1024];
  auto r = GetModuleFileNameExA(handle, NULL, buf, 1024);
  QString path = r ? QFileInfo(QString(buf)).dir().absolutePath() : QString();

  CloseHandle(handle);
  return path;
#else
  return QString();
#endif
}