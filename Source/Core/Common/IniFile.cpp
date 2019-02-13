// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "Common/IniFile.h"

#include <algorithm>
#include <cinttypes>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "Common/CommonTypes.h"
#include "Common/FileUtil.h"
#include "Common/StringUtil.h"

void IniFile::ParseLine(const std::string& line, std::string* keyOut, std::string* valueOut)
{
  if (line[0] == '#')
    return;

  size_t firstEquals = line.find('=');

  if (firstEquals != std::string::npos)
  {
    // Yes, a valid line!
    *keyOut = StripSpaces(line.substr(0, firstEquals));

    if (valueOut)
    {
      *valueOut = StripQuotes(StripSpaces(line.substr(firstEquals + 1, std::string::npos)));
    }
  }
}

const std::string& IniFile::NULL_STRING = "";

IniFile::Section::Section() = default;

IniFile::Section::Section(std::string name_) : name{std::move(name_)}
{
}

void IniFile::Section::Set(const std::string& key, std::string new_value)
{
  auto it = values.find(key);
  if (it != values.end())
    it->second = std::move(new_value);
  else
  {
    values[key] = std::move(new_value);
    keys_order.push_back(key);
  }
}

bool IniFile::Section::Get(const std::string& key, std::string* value,
                           const std::string& defaultValue) const
{
  auto it = values.find(key);
  if (it != values.end())
  {
    *value = it->second;
    return true;
  }
  else if (&defaultValue != &NULL_STRING)
  {
    *value = defaultValue;
    return true;
  }

  return false;
}

bool IniFile::Section::Exists(const std::string& key) const
{
  return values.find(key) != values.end();
}

bool IniFile::Section::Delete(const std::string& key)
{
  auto it = values.find(key);
  if (it == values.end())
    return false;

  values.erase(it);
  keys_order.erase(std::find(keys_order.begin(), keys_order.end(), key));
  return true;
}

void IniFile::Section::SetLines(std::vector<std::string> lines)
{
  m_lines = std::move(lines);
}

bool IniFile::Section::GetLines(std::vector<std::string>* lines, const bool remove_comments) const
{
  for (const std::string& line : m_lines)
  {
    std::string stripped_line = StripSpaces(line);

    if (remove_comments)
    {
      size_t commentPos = stripped_line.find('#');
      if (commentPos == 0)
      {
        continue;
      }

      if (commentPos != std::string::npos)
      {
        stripped_line = StripSpaces(stripped_line.substr(0, commentPos));
      }
    }

    lines->push_back(std::move(stripped_line));
  }

  return true;
}

// IniFile

const IniFile::Section* IniFile::GetSection(const std::string& sectionName) const
{
  for (const Section& sect : sections)
    if (!strcasecmp(sect.name.c_str(), sectionName.c_str()))
      return (&(sect));
  return nullptr;
}

IniFile::Section* IniFile::GetSection(const std::string& sectionName)
{
  for (Section& sect : sections)
    if (!strcasecmp(sect.name.c_str(), sectionName.c_str()))
      return (&(sect));
  return nullptr;
}

IniFile::Section* IniFile::GetOrCreateSection(const std::string& sectionName)
{
  Section* section = GetSection(sectionName);
  if (!section)
  {
    sections.emplace_back(sectionName);
    section = &sections.back();
  }
  return section;
}

bool IniFile::DeleteSection(const std::string& sectionName)
{
  Section* s = GetSection(sectionName);
  if (!s)
    return false;
  for (auto iter = sections.begin(); iter != sections.end(); ++iter)
  {
    if (&(*iter) == s)
    {
      sections.erase(iter);
      return true;
    }
  }
  return false;
}

bool IniFile::Exists(const std::string& sectionName, const std::string& key) const
{
  const Section* section = GetSection(sectionName);
  if (!section)
    return false;
  return section->Exists(key);
}

void IniFile::SetLines(const std::string& sectionName, const std::vector<std::string>& lines)
{
  Section* section = GetOrCreateSection(sectionName);
  section->SetLines(lines);
}

void IniFile::SetLines(const std::string& section_name, std::vector<std::string>&& lines)
{
  Section* section = GetOrCreateSection(section_name);
  section->SetLines(std::move(lines));
}

bool IniFile::DeleteKey(const std::string& sectionName, const std::string& key)
{
  Section* section = GetSection(sectionName);
  if (!section)
    return false;
  return section->Delete(key);
}

// Return a list of all keys in a section
bool IniFile::GetKeys(const std::string& sectionName, std::vector<std::string>* keys) const
{
  const Section* section = GetSection(sectionName);
  if (!section)
  {
    return false;
  }
  *keys = section->keys_order;
  return true;
}

// Return a list of all lines in a section
bool IniFile::GetLines(const std::string& sectionName, std::vector<std::string>* lines,
                       const bool remove_comments) const
{
  lines->clear();

  const Section* section = GetSection(sectionName);
  if (!section)
    return false;

  return section->GetLines(lines, remove_comments);
}

void IniFile::SortSections()
{
  sections.sort();
}

bool IniFile::Load(const std::string& filename, bool keep_current_data)
{
  if (!keep_current_data)
    sections.clear();
  // first section consists of the comments before the first real section

  // Open file
  std::ifstream in;
  File::OpenFStream(in, filename, std::ios::in);

  if (in.fail())
    return false;

  Section* current_section = nullptr;
  bool first_line = true;
  while (!in.eof())
  {
    std::string line;

    if (!std::getline(in, line))
    {
      if (in.eof())
        return true;
      else
        return false;
    }

    // Skips the UTF-8 BOM at the start of files. Notepad likes to add this.
    if (first_line && line.substr(0, 3) == "\xEF\xBB\xBF")
      line = line.substr(3);
    first_line = false;

#ifndef _WIN32
    // Check for CRLF eol and convert it to LF
    if (!line.empty() && line.back() == '\r')
    {
      line.pop_back();
    }
#endif

    if (!line.empty())
    {
      if (line[0] == '[')
      {
        size_t endpos = line.find(']');

        if (endpos != std::string::npos)
        {
          // New section!
          std::string sub = line.substr(1, endpos - 1);
          current_section = GetOrCreateSection(sub);
        }
      }
      else
      {
        if (current_section)
        {
          std::string key, value;
          ParseLine(line, &key, &value);

          // Lines starting with '$', '*' or '+' are kept verbatim.
          // Kind of a hack, but the support for raw lines inside an
          // INI is a hack anyway.
          if ((key.empty() && value.empty()) ||
              (!line.empty() && (line[0] == '$' || line[0] == '+' || line[0] == '*')))
            current_section->m_lines.push_back(line);
          else
            current_section->Set(key, value);
        }
      }
    }
  }

  in.close();
  return true;
}

bool IniFile::Save(const std::string& filename)
{
  std::ofstream out;
  std::string temp = File::GetTempFilenameForAtomicWrite(filename);
  File::OpenFStream(out, temp, std::ios::out);

  if (out.fail())
  {
    return false;
  }

  for (const Section& section : sections)
  {
    if (!section.keys_order.empty() || !section.m_lines.empty())
      out << '[' << section.name << ']' << std::endl;

    if (section.keys_order.empty())
    {
      for (const std::string& s : section.m_lines)
        out << s << std::endl;
    }
    else
    {
      for (const std::string& kvit : section.keys_order)
      {
        auto pair = section.values.find(kvit);
        out << pair->first << " = " << pair->second << std::endl;
      }
    }
  }

  out.close();

  return File::RenameSync(temp, filename);
}

// Unit test. TODO: Move to the real unit test framework.
/*
   int main()
   {
    IniFile ini;
    ini.Load("my.ini");
    ini.Set("Hello", "A", "amaskdfl");
    ini.Set("Moss", "A", "amaskdfl");
    ini.Set("Aissa", "A", "amaskdfl");
    //ini.Read("my.ini");
    std::string x;
    ini.Get("Hello", "B", &x, "boo");
    ini.DeleteKey("Moss", "A");
    ini.DeleteSection("Moss");
    ini.SortSections();
    ini.Save("my.ini");
    //UpdateVars(ini);
    return 0;
   }
 */