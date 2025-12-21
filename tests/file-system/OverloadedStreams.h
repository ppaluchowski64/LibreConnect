#ifndef OVERLOADED_STREAMS_H
#define OVERLOADED_STREAMS_H

#include <iostream>
#include "FileEntry.h"

std::ostream& operator<<(std::ostream& os, const FileType& type);
std::ostream& operator<<(std::ostream& os, const FileEntry& entry);

#endif // OVERLOADED_STREAMS_H
