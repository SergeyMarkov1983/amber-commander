#pragma once
#include "common.h"

enum class Overwrite { Ask, Always, Never, IfNewer };

struct CopyOptions {
    bool move = false;
    Overwrite overwrite = Overwrite::Ask;
};

struct CopyJob {
    std::wstring src;   // файл или каталог
    std::wstring dst;   // полный путь назначения
};

struct CopyProgress {
    uint64_t bytesDone = 0;
    uint64_t bytesTotal = 0;
    uint64_t filesDone = 0;
    uint64_t filesTotal = 0;
    double speedBps = 0;        // скользящее среднее
    std::wstring current;       // текущий файл
    int state = 0;              // 0=идёт, 1=пауза, 2=готово, 3=отменено, 4=ошибка
};

enum class AskChoice { Yes, YesAll, No, NoAll, Cancel };

using ProgressFn = std::function<void(const CopyProgress&)>;
// Вызывается из рабочего потока! UI должен маршалить (SendMessage + ожидание).
using AskFn = std::function<AskChoice(const std::wstring& src, const std::wstring& dst,
                                      uint64_t sizeSrc, uint64_t sizeDst,
                                      FILETIME tSrc, FILETIME tDst)>;

struct CopyOutcome {
    bool ok = true;
    bool canceled = false;
    std::vector<std::wstring> errors;
};

// Выполняет список заданий копирования/перемещения в вызывающем потоке.
CopyOutcome RunCopy(const std::vector<CopyJob>& jobs, const CopyOptions& opt,
                    ProgressFn onProgress, AskFn onAsk,
                    std::atomic<bool>& cancel, std::atomic<bool>& pause);

// Удаление в Корзину (FO_ALLOWUNDO). Возвращает список ошибок.
std::vector<std::wstring> DeleteToRecycleBin(const std::vector<std::wstring>& paths);
// Прямое удаление (рекурсивное). Возвращает список ошибок.
std::vector<std::wstring> DeleteDirect(const std::vector<std::wstring>& paths);

// Суммарный размер дерева (файлы внутри каталогов).
uint64_t CalcTreeSize(const std::wstring& path, std::atomic<bool>* cancel = nullptr);
