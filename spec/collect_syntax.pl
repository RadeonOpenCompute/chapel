#!/usr/bin/perl
#

`rm -f Syntax.tex`;
$i = 0;
@texs = `ls *.tex`;
foreach $tex (@texs) {
    $readSyntax = 0;
    $rule = "";
    @lines = `cat $tex`;
    foreach $line (@lines) {
        if ($line =~ /^\\begin\{syntax\}/) {
            die "unmatched begin of syntax block" if ($readSyntax == 1);
            $readSyntax = 1;
        } elsif ($line =~ /^\\end\{syntax\}/) {
            die "unmatched end of syntax block" if ($readSyntax == 0);
            $readSyntax = 0;
            $rules{$i++} = $rule;
            $rule = "";
        } elsif ($readSyntax == 1) {
            if ($line =~ /^\s*$/) {
                $rules{$i++} = $rule;
                $rule = "";
            } else {
                $rule .= $line;
            }
        }
    }
}

open FILE, ">Syntax.tex";
print FILE "%%\n";
print FILE "%% Do not modify this file.  This file is automatically\n";
print FILE "%% generated by collect_syntax.pl.\n";
print FILE "%%\n\n";
print FILE "\\sekshun{Collected Syntax}\n";
print FILE "\\label{Syntax}\n\n";
print FILE "This appendix collects the syntax productions listed throughout the specification.  There are no new syntax productions in this appendix.  The productions are listed alphabetically for reference.\n\\vspace{1pc}\n\n";
$last = "";
foreach $rule (sort values %rules) {
    $prefix = $last;
    if ($prefix =~ m/(.*:)/) {
        if ($rule =~ m/^$1/) {
            if (!($rule eq $last)) {
                print "Syntax rules do not match\n";
                print "$last$rule";
            }
            $duplicate = 1;
        }
    }
    if ($duplicate == 0) {
        print FILE "\\begin{syntax}\n";
        print FILE "$rule";
        print FILE "\\end{syntax}\n\n";
    }
    $duplicate = 0;
    $last = $rule;
}
close FILE;

print "Collected $i grammar rules in Syntax.tex\n";
