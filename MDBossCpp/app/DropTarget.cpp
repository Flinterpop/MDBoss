#include "DropTarget.h"

#include <vector>

#include "FileScan.h"

namespace mdboss {

bool DocumentDropTarget::OnDropFiles(wxCoord, wxCoord,
                                     const wxArrayString& filenames)
{
    std::vector<std::string> names;
    names.reserve(filenames.GetCount());
    for (const wxString& name : filenames) {
        names.push_back(std::string(name.ToUTF8()));
    }
    const std::string chosen = choose_dropped_file(names);
    if (chosen.empty() || !on_drop_) {
        return false;
    }
    on_drop_(chosen);
    return true;
}

}  // namespace mdboss
