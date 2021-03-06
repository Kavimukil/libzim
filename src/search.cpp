/*
 * Copyright (C) 2007 Tommi Maekitalo
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * is provided AS IS, WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, and
 * NON-INFRINGEMENT.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#include <zim/search.h>
#include "search_internal.h"

#include <sstream>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#if defined(ENABLE_XAPIAN)
#include "xapian.h"
#include <unicode/locid.h>
#endif

namespace zim
{

namespace {

/* Split string in a token array */
std::vector<std::string> split(const std::string & str,
                                const std::string & delims=" *-")
{
  std::string::size_type lastPos = str.find_first_not_of(delims, 0);
  std::string::size_type pos = str.find_first_of(delims, lastPos);
  std::vector<std::string> tokens;

  while (std::string::npos != pos || std::string::npos != lastPos)
    {
      tokens.push_back(str.substr(lastPos, pos - lastPos));
      lastPos = str.find_first_not_of(delims, pos);
      pos     = str.find_first_of(delims, lastPos);
    }

  return tokens;
}

std::map<std::string, int> read_valuesmap(const std::string &s) {
    std::map<std::string, int> result;
    std::vector<std::string> elems = split(s, ";");
    for(std::vector<std::string>::iterator elem = elems.begin();
        elem != elems.end();
        elem++)
    {
        std::vector<std::string> tmp_elems = split(*elem, ":");
        result.insert( std::pair<std::string, int>(tmp_elems[0], atoi(tmp_elems[1].c_str())) );
    }
    return result;
}
}

Search::Search(const std::vector<const File*> zimfiles) :
    internal(new InternalData),
    zimfiles(zimfiles),
    suggestion_mode(false),
    search_started(false),
    has_database(false)
{}

Search::Search(const File* zimfile) :
    internal(new InternalData),
    suggestion_mode(false),
    search_started(false),
    has_database(false)
{
    zimfiles.push_back(zimfile);
}

Search::Search(const Search& it) :
     internal(new InternalData),
     zimfiles(it.zimfiles),
     query(it.query),
     range_start(it.range_start),
     range_end(it.range_end),
     suggestion_mode(it.suggestion_mode),
     search_started(false),
     has_database(false)
{ }

Search& Search::operator=(const Search& it)
{
     if ( internal ) internal.reset();
     zimfiles = it.zimfiles;
     query = it.query;
     range_start = it.range_start;
     range_end = it.range_end;
     suggestion_mode = it.suggestion_mode;
     search_started = false;
     has_database = false;
     return *this;
}

Search::Search(Search&& it) = default;
Search& Search::operator=(Search&& it) = default;
Search::~Search() = default;

Search& Search::add_zimfile(const File* zimfile) {
    zimfiles.push_back(zimfile);
    return *this;
}

Search& Search::set_query(const std::string& query) {
    this->query = query; 
    return *this;
}

Search& Search::set_range(int start, int end) {
    this->range_start = start;
    this->range_end = end; 
    return *this;
}

Search& Search::set_suggestion_mode(const bool suggestion_mode) {
    this->suggestion_mode = suggestion_mode;
    return *this;
}


#if defined(ENABLE_XAPIAN)
void
setup_queryParser(Xapian::QueryParser* queryparser,
                  Xapian::Database& database,
                  const std::string& language,
                  const std::string& stopwords) {
    queryparser->set_database(database);
    if ( ! language.empty() )
    {
        /* Build ICU Local object to retrieve ISO-639 language code (from
           ISO-639-3) */
        icu::Locale languageLocale(language.c_str());

        /* Configuring language base steemming */
        try {
            Xapian::Stem stemmer = Xapian::Stem(languageLocale.getLanguage());
            queryparser->set_stemmer(stemmer);
            queryparser->set_stemming_strategy(Xapian::QueryParser::STEM_ALL);
        } catch (...) {
            std::cout << "No steemming for language '" << languageLocale.getLanguage() << "'" << std::endl;
        }
    }

    if ( ! stopwords.empty() )
    {
        std::string stopWord;
        std::istringstream file(stopwords);
        Xapian::SimpleStopper* stopper = new Xapian::SimpleStopper();
        while (std::getline(file, stopWord, '\n')) {
            stopper->add(stopWord);
        }
        stopper->release();
        queryparser->set_stopper(stopper);
    }
}
#endif

Search::iterator Search::begin() const {
#if defined(ENABLE_XAPIAN)
    if ( this->search_started ) {
        return new search_iterator::InternalData(this, internal->results.begin());
    }

    std::vector<const File*>::const_iterator it;
    bool first = true;
    std::string language;
    std::string stopwords;
    for(it=zimfiles.begin(); it!=zimfiles.end(); it++)
    {
        const File* zimfile = *it;
        zim::Article xapianArticle = zimfile->getArticle('Z', "/fulltextIndex/xapian");
        if (!xapianArticle.good()) {
            continue;
        }
        zim::offset_type dbOffset = xapianArticle.getOffset();
        int databasefd = open(zimfile->getFilename().c_str(), O_RDONLY);
        lseek(databasefd, dbOffset, SEEK_SET);
        Xapian::Database database(databasefd);
        if ( first ) {
            this->valuesmap = read_valuesmap(database.get_metadata("valuesmap"));
            language = database.get_metadata("language");
            stopwords = database.get_metadata("stopwords");
            this->prefixes = database.get_metadata("prefixes");
        } else {
            std::map<std::string, int> valuesmap = read_valuesmap(database.get_metadata("valuesmap"));
            if (this->valuesmap != valuesmap ) {
                // [TODO] Ignore the database, raise a error ?
            }
        }
        internal->xapian_databases.push_back(database);
        internal->database.add_database(database);
        has_database = true;
    }

    if ( ! has_database ) {
        estimated_matches_number = 0;
        return nullptr;
    }
    
    Xapian::QueryParser* queryParser = new Xapian::QueryParser();
    setup_queryParser(queryParser, internal->database, language, stopwords);

    std::string prefix = "";
    unsigned flags = Xapian::QueryParser::FLAG_DEFAULT;
    if (suggestion_mode) {
      flags |= Xapian::QueryParser::FLAG_PARTIAL;
      if (this->prefixes.find("S") != std::string::npos ) {
        prefix = "S";
      }
    }
    Xapian::Query query = queryParser->parse_query(this->query, flags, prefix);
    delete queryParser;
    
    Xapian::Enquire enquire(internal->database);
    enquire.set_query(query);
    
    internal->results = enquire.get_mset(this->range_start, this->range_end-this->range_start);
    search_started = true;
    estimated_matches_number = internal->results.get_matches_estimated();
    return new search_iterator::InternalData(this, internal->results.begin());
#else
    estimated_matches_number = 0;
    return nullptr;
#endif
}

Search::iterator Search::end() const {
#if defined(ENABLE_XAPIAN)
    if ( ! has_database ) {
        return nullptr;
    }
    return new search_iterator::InternalData(this, internal->results.end());
#else
    return nullptr;
#endif
}

int Search::get_matches_estimated() const {
    // Ensure that the search as begin
    begin();
    return estimated_matches_number;
}

} //namespace zim
