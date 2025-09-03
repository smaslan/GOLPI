% reorder PATH entries so Octave...usr/bin is on top (needed to make pkg install work!)
_paths = strsplit(getenv("PATH"),';');
[~,_pathids,~] = unique(_paths,'first');
_paths = _paths(sort(_pathids));
if exist(fullfile(OCTAVE_HOME,'usr','bin')), _paths = cat(2,_paths,fullfile(OCTAVE_HOME,'usr','bin')); endif;
if exist(fullfile(fileparts(OCTAVE_HOME),'usr','bin')), _paths = cat(2,_paths,fullfile(fileparts(OCTAVE_HOME),'usr','bin')); endif;
[~,_pathids,~] = unique(_paths,'first');
_paths = _paths(sort(_pathids));
[~,_pathids] = sort(cellfun(@isempty,regexp(_paths,'[Oo]ctave.+\\usr\\bin|[Oo]ctave.+\\bin')));
_paths = _paths(_pathids);
setenv("PATH",strjoin(_paths,';'));
clear _paths _pathids;
