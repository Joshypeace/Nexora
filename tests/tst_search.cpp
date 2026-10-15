// Search and filtering: case-insensitivity, relevance ordering, initials, hidden apps.

#include "AppModel.h"

#include <QtTest>

class SearchTest : public QObject {
    Q_OBJECT

private:
    static AppEntry app(const QString& name)
    {
        AppEntry entry;
        entry.id = QStringLiteral("app:") + name.toLower();
        entry.name = name;
        return entry;
    }

    QStringList visibleNames() const
    {
        QStringList names;
        for (int row = 0; row < m_filter.rowCount(); ++row)
            names.append(m_filter.index(row, 0).data(Qt::DisplayRole).toString());
        return names;
    }

    AppModel m_model;
    AppFilterModel m_filter;

private slots:
    void initTestCase()
    {
        m_model.setApps({app(QStringLiteral("Visual Studio Code")), app(QStringLiteral("Visual Studio 2026")),
                         app(QStringLiteral("Calculator")), app(QStringLiteral("Notepad")),
                         app(QStringLiteral("Code Writer")), app(QStringLiteral("Google Chrome")),
                         app(QStringLiteral("Paint 10")), app(QStringLiteral("Paint 9"))},
                        1.0);
        m_filter.setSourceModel(&m_model);
    }

    void cleanup()
    {
        m_filter.setQuery({});
        m_filter.setHiddenIds({});
    }

    void emptyQueryShowsEverythingAlphabetically()
    {
        QCOMPARE(visibleNames(),
                 QStringList({"Calculator", "Code Writer", "Google Chrome", "Notepad", "Paint 9", "Paint 10",
                              "Visual Studio 2026", "Visual Studio Code"}));
    }

    void isCaseInsensitive()
    {
        m_filter.setQuery(QStringLiteral("CALC"));
        QCOMPARE(visibleNames(), QStringList({"Calculator"}));
        m_filter.setQuery(QStringLiteral("  notePAD "));
        QCOMPARE(visibleNames(), QStringList({"Notepad"}));
    }

    void prefixMatchesRankFirst()
    {
        m_filter.setQuery(QStringLiteral("code"));
        QCOMPARE(visibleNames(), QStringList({"Code Writer", "Visual Studio Code"}));
    }

    void ranksPrefixThenWordStartThenSubstring()
    {
        AppModel model;
        model.setApps({app(QStringLiteral("Keynote")), app(QStringLiteral("Piano Notes")), app(QStringLiteral("Notepad"))},
                      1.0);
        AppFilterModel filter;
        filter.setSourceModel(&model);
        filter.setQuery(QStringLiteral("note"));
        QStringList names;
        for (int row = 0; row < filter.rowCount(); ++row)
            names.append(filter.index(row, 0).data().toString());
        QCOMPARE(names, QStringList({"Notepad", "Piano Notes", "Keynote"}));
    }

    void matchesInitials()
    {
        m_filter.setQuery(QStringLiteral("vsc"));
        QCOMPARE(visibleNames(), QStringList({"Visual Studio Code"}));
    }

    void requiresEveryTerm()
    {
        m_filter.setQuery(QStringLiteral("studio code"));
        QCOMPARE(visibleNames(), QStringList({"Visual Studio Code"}));
    }

    void reportsNoMatches()
    {
        m_filter.setQuery(QStringLiteral("zzz"));
        QVERIFY(visibleNames().isEmpty());
    }

    void excludesHiddenApps()
    {
        m_filter.setHiddenIds({QStringLiteral("app:notepad")});
        QVERIFY(!visibleNames().contains(QStringLiteral("Notepad")));
        QCOMPARE(visibleNames().size(), 7);
    }

    void exposesEntriesThroughTheProxy()
    {
        m_filter.setQuery(QStringLiteral("chrome"));
        const AppEntry* entry = m_model.entry(m_filter.mapToSource(m_filter.index(0, 0)));
        QVERIFY(entry);
        QCOMPARE(entry->id, QStringLiteral("app:google chrome"));
    }
};

QTEST_MAIN(SearchTest)
#include "tst_search.moc"
