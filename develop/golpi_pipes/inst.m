clear all;
more off;

mfld = fileparts(mfilename('fullpath'))
package_file = fullfile(mfld, '..', '..', 'GOLPI', 'Octave package', 'golpi-1.3.0.tar.gz')
  
try
    pkg('uninstall','golpi');
end
pkg('install',package_file)