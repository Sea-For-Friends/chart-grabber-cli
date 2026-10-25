# REMOVE-AFTER: next release. T-0313: python side of tools/lib/env_compat.sh (same names file, same rules).
# `import env_compat` (tools/lib is put on sys.path by the importer) maps SAS_<X> -> CG_<X> in os.environ in place, so every
# os.environ read and every child process sees the CG_* names; one stderr warning per process tree.
import os
import sys


def apply(environ=None):
    env = os.environ if environ is None else environ
    names_file = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'env_compat.names')
    mapped = []
    with open(names_file, encoding='utf-8') as f:
        for line in f:
            x = line.strip()
            if not x or x.startswith('#'):
                continue
            if env.get('SAS_' + x) and not env.get('CG_' + x):
                env['CG_' + x] = env['SAS_' + x]
                mapped.append(x)
    if mapped and not env.get('CG_ENV_COMPAT_WARNED'):
        more = ' (and %d more)' % (len(mapped) - 1) if len(mapped) > 1 else ''
        print('warning: SAS_%s%s is deprecated, use CG_%s; the SAS_* names are removed in the next release'
              % (mapped[0], more, mapped[0]), file=sys.stderr)
        env['CG_ENV_COMPAT_WARNED'] = '1'
    return mapped


apply()
