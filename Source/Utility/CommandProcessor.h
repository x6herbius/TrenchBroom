/*
 Copyright (C) 2010-2012 Kristian Duske
 
 This file is part of TrenchBroom.
 
 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 
 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with TrenchBroom.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __TrenchBroom__CommandProcessor__
#define __TrenchBroom__CommandProcessor__

#include <wx/cmdproc.h>

#include <vector>

typedef std::vector<wxCommand*> CommandList;

class CompoundCommand : public wxCommand {
protected:
    CommandList m_commands;
public:
    CompoundCommand(const wxString& name);
    ~CompoundCommand();
    
    void addCommand(wxCommand* command);
    
    bool Do();
    bool Undo();
};

class CommandProcessor : public wxCommandProcessor {
protected:
    unsigned int m_groupLevel;
    CompoundCommand* m_compoundCommand;
public:
    CommandProcessor(int maxCommandLevel = -1);

    static void BeginGroup(wxCommandProcessor* wxCommandProc, const wxString& name);
    static void EndGroup(wxCommandProcessor* wxCommandProc);
    static void CancelGroup(wxCommandProcessor* wxCommandProc);
    
    void BeginGroup(const wxString& name);
    void EndGroup();
    void CancelGroup();
    bool Submit(wxCommand* command, bool storeIt = true);
};

#endif /* defined(__TrenchBroom__CommandProcessor__) */