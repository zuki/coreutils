#!/usr/bin/perl
# Test the diagnostics of "test".

# Copyright (C) 2006-2025 Free Software Foundation, Inc.

# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

use strict;

(my $program_name = $0) =~ s|.*/||;

# Turn off localization of executable's output.
@ENV{qw(LANGUAGE LANG LC_ALL)} = ('C') x 3;

my @Tests =
    (
     # In coreutils-5.93, this diagnostic lacked the newline.
     ['o', '-o arg', {ERR => "test: '-o': unary operator expected\n"},
      {ERR_SUBST => 's!^.*test:!test:!'},
      {EXIT => 2}],
    );

my $save_temps = $ENV{DEBUG};
my $verbose = $ENV{VERBOSE};
my $prog = 'test';
my $fail = run_tests ($program_name, \$prog, \@Tests, $save_temps, $verbose);
exit $fail;
