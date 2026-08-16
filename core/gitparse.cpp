// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "gitparse.h"

#include <QRegularExpression>

#include <algorithm>

namespace {

const QChar FieldSeparator = QChar(0x1f);
const char RecordSeparator = '\x1e';

// Field separator used by ref and stash formats.
const QChar RefFieldSeparator = QChar(0x01);

// The body is separate because --name-status appends its records to it.
GitCommitInfo commitFromFields(const QStringList &fields, const QString &body) {
    GitCommitInfo commit;
    commit.hash = fields.at(0).trimmed();
    commit.shortHash = fields.at(1);
    commit.parents = fields.at(2).split(u' ', Qt::SkipEmptyParts);
    commit.author = fields.at(3);
    commit.authorEmail = fields.at(4);
    commit.committer = fields.at(5);
    commit.committerEmail = fields.at(6);
    commit.authoredAt = QDateTime::fromString(fields.at(7), Qt::ISODate);
    commit.committedAt = QDateTime::fromString(fields.at(8), Qt::ISODate);
    commit.references = fields.at(9);
    commit.subject = fields.at(10);
    commit.body = body;
    return commit;
}

}

namespace GitParse {

QStringList commitFormatArguments() {
    return {
        QStringLiteral("--date=iso-strict"),
        QStringLiteral("--decorate=short"),
        QStringLiteral("--pretty=format:%x1e%H%x1f%h%x1f%P%x1f%an%x1f%ae%x1f%cn%x1f%ce"
                       "%x1f%aI%x1f%cI%x1f%D%x1f%s%x1f%b")
    };
}

QList<GitCommitInfo> parseCommits(const QByteArray &payload) {
    QList<GitCommitInfo> commits;
    const QList<QByteArray> records = payload.split(RecordSeparator);
    for (const QByteArray &record : records) {
        if (record.trimmed().isEmpty()) {
            continue;
        }

        const QStringList fields = QString::fromUtf8(record).split(FieldSeparator,
                                                                   Qt::KeepEmptyParts);
        if (fields.size() < 12) {
            continue;
        }

        commits.append(commitFromFields(fields, fields.mid(11).join(FieldSeparator).trimmed()));
    }
    return commits;
}

QList<GitTreeEntry> parseTreeEntries(const QByteArray &payload) {
    QList<GitTreeEntry> entries;

    for (const QByteArray &record : splitNulRecords(payload)) {
        // "<mode> <type> <object>  <size>\t<path>", the size column is padded.
        const qsizetype tab = record.indexOf('\t');
        if (tab < 0) {
            continue;
        }

        const QList<QByteArray> header = record.left(tab).simplified().split(' ');
        if (header.size() < 3) {
            continue;
        }

        GitTreeEntry entry;
        const QByteArray &type = header.at(1);
        entry.directory = type == "tree";
        entry.submodule = type == "commit";
        entry.hash = QString::fromLatin1(header.at(2));
        if (header.size() > 3) {
            bool parsed = false;
            const qint64 size = header.at(3).toLongLong(&parsed);
            if (parsed) {
                entry.size = size;
            }
        }
        entry.path = QString::fromUtf8(record.mid(tab + 1));
        entry.name = entry.path.section(u'/', -1);
        entries.append(entry);
    }

    sortTreeEntries(entries);
    return entries;
}

void sortTreeEntries(QList<GitTreeEntry> &entries) {
    std::sort(entries.begin(), entries.end(),
              [](const GitTreeEntry &left, const GitTreeEntry &right) {
                  if (left.directory != right.directory) {
                      return left.directory;
                  }
                  const int order = QString::compare(left.name, right.name,
                                                     Qt::CaseInsensitive);
                  return order != 0 ? order < 0 : left.name < right.name;
              });
}

QList<GitFileRevision> parseFileHistory(const QByteArray &payload) {
    QList<GitFileRevision> revisions;

    for (const QByteArray &record : payload.split(RecordSeparator)) {
        if (record.trimmed().isEmpty()) {
            continue;
        }

        const QStringList fields = QString::fromUtf8(record).split(FieldSeparator,
                                                                   Qt::KeepEmptyParts);
        if (fields.size() < 12) {
            continue;
        }

        // The last field holds the commit body, then a newline, then the
        // NUL separated "--name-status -z" records of that commit.
        const QString tail = fields.mid(11).join(FieldSeparator);
        QString body = tail;
        QString changes;
        const qsizetype firstNul = tail.indexOf(QChar(u'\0'));
        if (firstNul >= 0) {
            const qsizetype bodyEnd = tail.lastIndexOf(u'\n', firstNul);
            body = bodyEnd > 0 ? tail.first(bodyEnd) : QString();
            changes = tail.sliced(bodyEnd + 1);
        }

        GitFileRevision revision;
        revision.commit = commitFromFields(fields, body.trimmed());

        const QStringList records = changes.split(QChar(u'\0'), Qt::SkipEmptyParts);
        if (!records.isEmpty()) {
            revision.status = records.first().at(0);
            // A rename or copy spends three records: the marker, the path
            // before the commit and the path the file carries afterwards.
            if (revision.isRename() && records.size() >= 3) {
                revision.previousPath = records.at(1);
                revision.path = records.at(2);
            } else if (records.size() >= 2) {
                revision.path = records.at(1);
            }
        }

        revisions.append(revision);
    }

    // Merge commits reach the timeline without a diff of their own. They take
    // the path the file carried just after them, so the viewer always has a
    // path to read; across a rename that is the older name.
    for (qsizetype index = 0; index < revisions.size(); ++index) {
        if (!revisions.at(index).path.isEmpty() || index == 0) {
            continue;
        }
        const GitFileRevision &newer = revisions.at(index - 1);
        revisions[index].path = newer.isRename() ? newer.previousPath : newer.path;
    }

    return revisions;
}

/// Offset just past the @p count-th space, or -1 when the record holds fewer.
/// The path is the last field and may hold spaces, so it is never split.
qsizetype offsetAfterFields(const QByteArray &record, const int count) {
    qsizetype offset = 0;
    for (int field = 0; field < count; ++field) {
        const qsizetype space = record.indexOf(' ', offset);
        if (space < 0) {
            return -1;
        }
        offset = space + 1;
    }
    return offset;
}

/// Version 2 writes an unmodified side as '.', version 1 left a space. One
/// representation everywhere else keeps the status predicates unchanged.
QChar normalizedState(const char state) {
    return state == '.' ? u' ' : QChar::fromLatin1(state);
}

void readCommonFields(const QByteArray &record, GitFileStatus *file) {
    file->indexStatus = normalizedState(record.at(2));
    file->workTreeStatus = normalizedState(record.at(3));
    // The submodule field is "N..." for anything that is not one.
    file->submodule = record.size() > 5 && record.at(5) == 'S';
}

QList<GitFileStatus> parseStatus(const QByteArray &payload) {
    QList<GitFileStatus> files;
    const QList<QByteArray> records = splitNulRecords(payload);

    for (qsizetype index = 0; index < records.size(); ++index) {
        const QByteArray &record = records.at(index);
        if (record.size() < 2) {
            continue;
        }

        const char kind = record.at(0);
        if (kind == '#' || kind == '!') {
            // Branch headers and ignored paths are of no interest here.
            continue;
        }

        GitFileStatus file;
        if (kind == '?') {
            file.indexStatus = u'?';
            file.workTreeStatus = u'?';
            file.path = QString::fromUtf8(record.mid(2));
            files.append(file);
            continue;
        }

        // Fields before the path: a rename adds the score, an unmerged path
        // carries three stages instead of one.
        const int leadingFields = kind == '1' ? 8 : (kind == '2' ? 9 : 10);
        if (kind != '1' && kind != '2' && kind != 'u') {
            continue;
        }
        if (record.size() < 6) {
            continue;
        }

        const qsizetype pathOffset = offsetAfterFields(record, leadingFields);
        if (pathOffset < 0) {
            continue;
        }

        readCommonFields(record, &file);
        file.path = QString::fromUtf8(record.mid(pathOffset));

        if (kind == '2') {
            // The score reads "R100" or "C75"; the original path follows.
            const qsizetype scoreOffset = offsetAfterFields(record, 8);
            if (scoreOffset > 0) {
                file.renameScore =
                    record.mid(scoreOffset + 1, pathOffset - scoreOffset - 2).toInt();
            }
            if (index + 1 < records.size()) {
                file.originalPath = QString::fromUtf8(records.at(++index));
            }
        }

        files.append(file);
    }

    return files;
}

QList<GitBranchInfo> parseBranches(const QString &payload) {
    QList<GitBranchInfo> branches;
    const QStringList lines = payload.split(u'\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QStringList fields = line.split(RefFieldSeparator);
        if (fields.size() < 7) {
            continue;
        }

        GitBranchInfo branch;
        branch.current = fields.at(0).trimmed() == QStringLiteral("*");
        branch.name = fields.at(1);
        branch.hash = fields.at(2);
        branch.upstream = fields.at(3);
        parseTrackInformation(fields.at(4), &branch.ahead, &branch.behind);
        branch.subject = fields.at(5);
        branch.committedAt = QDateTime::fromString(fields.at(6), Qt::ISODate);
        if (!branch.name.isEmpty()) {
            branches.append(branch);
        }
    }
    return branches;
}

QList<GitTagInfo> parseTags(const QString &payload) {
    QList<GitTagInfo> tags;
    const QStringList lines = payload.split(u'\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QStringList fields = line.split(RefFieldSeparator);
        if (fields.size() < 4) {
            continue;
        }
        GitTagInfo tag;
        tag.name = fields.at(0);
        tag.hash = fields.at(1);
        tag.subject = fields.at(2);
        tag.taggedAt = QDateTime::fromString(fields.at(3), Qt::ISODate);
        tags.append(tag);
    }
    return tags;
}

QList<GitStashInfo> parseStashes(const QString &payload) {
    QList<GitStashInfo> stashes;
    const QStringList lines = payload.split(u'\n', Qt::SkipEmptyParts);
    int index = 0;
    for (const QString &line : lines) {
        const QStringList fields = line.split(RefFieldSeparator);
        if (fields.size() < 3) {
            continue;
        }

        GitStashInfo stash;
        stash.index = index++;
        stash.reference = fields.at(0);
        stash.createdAt = QDateTime::fromString(fields.at(2), Qt::ISODate);

        QString subject = fields.at(1);
        static const QRegularExpression prefixPattern(
            QStringLiteral("^(?:WIP on|On) ([^:]+):\\s*"));
        const QRegularExpressionMatch match = prefixPattern.match(subject);
        if (match.hasMatch()) {
            stash.branch = match.captured(1);
            subject = subject.mid(match.capturedLength());
        }
        stash.message = subject;
        stashes.append(stash);
    }
    return stashes;
}

QList<GitSubmoduleInfo> parseSubmodules(const QString &payload) {
    // A line is "<state><hash> <path>" with an optional " (<describe>)" tail.
    // The path may hold spaces, so it is the remainder, not a field.
    constexpr qsizetype HashLength = 40;

    QList<GitSubmoduleInfo> submodules;
    const QStringList lines = payload.split(u'\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        // The state character is absent when the submodule is in sync.
        const QString body = line.startsWith(u' ') || line.startsWith(u'-')
                                     || line.startsWith(u'+') || line.startsWith(u'U')
                                 ? line.mid(1)
                                 : line;
        if (body.size() < HashLength + 2) {
            continue;
        }

        GitSubmoduleInfo submodule;
        submodule.hash = body.left(HashLength);
        QString remainder = body.mid(HashLength + 1);
        if (remainder.endsWith(u')')) {
            const qsizetype describeStart = remainder.lastIndexOf(QStringLiteral(" ("));
            if (describeStart > 0) {
                submodule.describe = remainder.mid(describeStart + 2,
                                                   remainder.size() - describeStart - 3);
                remainder = remainder.left(describeStart);
            }
        }
        submodule.path = remainder;
        submodules.append(submodule);
    }
    return submodules;
}

QList<GitRemoteInfo> parseRemotes(const QString &payload) {
    QList<GitRemoteInfo> remotes;
    const QStringList lines = payload.split(u'\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        // A tab separates the name from the URL. Splitting on whitespace
        // would cut a URL holding a space, as local paths often do.
        const qsizetype tab = line.indexOf(u'\t');
        if (tab <= 0) {
            continue;
        }
        QString url = line.mid(tab + 1);
        const qsizetype role = url.lastIndexOf(QStringLiteral(" ("));
        if (role > 0 && url.endsWith(u')')) {
            url = url.left(role);
        }
        const QStringList fields{line.left(tab), url};
        const auto existing = std::find_if(remotes.begin(), remotes.end(),
                                           [&fields](const GitRemoteInfo &remote) {
                                               return remote.name == fields.at(0);
                                           });
        if (existing == remotes.end()) {
            GitRemoteInfo remote;
            remote.name = fields.at(0);
            remote.url = fields.at(1);
            remotes.append(remote);
        }
    }
    return remotes;
}

void assignRemoteBranches(QList<GitRemoteInfo> &remotes, const QString &payload) {
    const QStringList references = payload.split(u'\n', Qt::SkipEmptyParts);
    for (const QString &reference : references) {
        if (reference.endsWith(QStringLiteral("/HEAD"))) {
            continue;
        }
        const qsizetype separator = reference.indexOf(u'/');
        if (separator <= 0) {
            continue;
        }
        const QString remoteName = reference.left(separator);
        for (GitRemoteInfo &remote : remotes) {
            if (remote.name == remoteName) {
                remote.branches.append(reference.mid(separator + 1));
                break;
            }
        }
    }
}

QList<GitChangedFile> parseNameStatus(const QByteArray &payload,
                                      const bool renamesCarryOriginal) {
    QList<GitChangedFile> files;
    const QList<QByteArray> records = splitNulRecords(payload);

    if (!renamesCarryOriginal) {
        for (qsizetype position = 0; position + 1 < records.size(); position += 2) {
            const QString marker = QString::fromUtf8(records.at(position)).trimmed();
            if (marker.isEmpty()) {
                continue;
            }
            GitChangedFile file;
            file.status = marker.at(0);
            file.path = QString::fromUtf8(records.at(position + 1));
            files.append(file);
        }
        return files;
    }

    for (qsizetype index = 0; index < records.size(); ++index) {
        const QString marker = QString::fromUtf8(records.at(index)).trimmed();
        if (marker.isEmpty()) {
            continue;
        }

        GitChangedFile file;
        file.status = marker.at(0);
        const bool isRenameOrCopy = file.status == u'R' || file.status == u'C';
        if (isRenameOrCopy) {
            if (index + 2 >= records.size()) {
                break;
            }
            file.originalPath = QString::fromUtf8(records.at(++index));
            file.path = QString::fromUtf8(records.at(++index));
        } else {
            if (index + 1 >= records.size()) {
                break;
            }
            file.path = QString::fromUtf8(records.at(++index));
        }
        files.append(file);
    }

    return files;
}

void assignChangeCounts(QList<GitChangedFile> &files, const QByteArray &payload) {
    // Under -z a record is "<added>\t<removed>\t<path>". A rename leaves the
    // path empty and sends the old and new paths as the next two records.
    const QList<QByteArray> records = splitNulRecords(payload);

    for (qsizetype index = 0; index < records.size(); ++index) {
        const QList<QByteArray> fields = records.at(index).split('\t');
        if (fields.size() < 3) {
            continue;
        }

        QString path = QString::fromUtf8(fields.at(2));
        if (path.isEmpty()) {
            if (index + 2 >= records.size()) {
                break;
            }
            // Counts belong to the new path.
            ++index;
            path = QString::fromUtf8(records.at(++index));
        }

        const QString added = QString::fromUtf8(fields.at(0));
        for (GitChangedFile &file : files) {
            if (file.path != path) {
                continue;
            }
            // A binary file reports "-" for both counts.
            file.binary = added == QStringLiteral("-");
            file.additions = added.toInt();
            file.deletions = QString::fromUtf8(fields.at(1)).toInt();
            break;
        }
    }
}

void parseTrackInformation(const QString &track, int *ahead, int *behind) {
    *ahead = 0;
    *behind = 0;
    if (track.isEmpty()) {
        return;
    }

    static const QRegularExpression aheadPattern(QStringLiteral("ahead (\\d+)"));
    static const QRegularExpression behindPattern(QStringLiteral("behind (\\d+)"));
    const QRegularExpressionMatch aheadMatch = aheadPattern.match(track);
    if (aheadMatch.hasMatch()) {
        *ahead = aheadMatch.captured(1).toInt();
    }
    const QRegularExpressionMatch behindMatch = behindPattern.match(track);
    if (behindMatch.hasMatch()) {
        *behind = behindMatch.captured(1).toInt();
    }
}

QList<QByteArray> splitNulRecords(const QByteArray &payload) {
    QList<QByteArray> records = payload.split('\0');
    while (!records.isEmpty() && records.constLast().isEmpty()) {
        records.removeLast();
    }
    return records;
}

}
