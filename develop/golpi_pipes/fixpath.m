% reorder PATH entries so Octave...usr/bin is on top (needed to make pkg install work!)
_paths = unique(strsplit(getenv("PATH"),';'),'stable');
_usrlist = cellfun(@isempty,regexp(_paths,'[Oo]ctave.+\\usr\\bin|[Oo]ctave.+\\bin'));
setenv("PATH",[strcat(_paths([find(~_usrlist) find(_usrlist)]),';'){:}]);
clear _paths _usrlist;