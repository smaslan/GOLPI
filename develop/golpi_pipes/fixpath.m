% reorder PATH entries so Octave...usr/bin is on top (needed to make pkg install work!)
_paths = unique(strsplit(getenv("PATH"),';'),'stable');
[~,_pathids] = sort(cellfun(@isempty,regexp(_paths,'[Oo]ctave.+\\usr\\bin|[Oo]ctave.+\\bin')));
setenv("PATH",strjoin(_paths(_pathids),';'));
clear _paths _pathids;