#!/usr/bin/env perl
use strict; use warnings; use File::Basename;
our ($mock_term,$pass,$fail) = ($ENV{MOCK_TERM} || './t/mx-test', 0, 0);

sub run_mock {
    my ($in,$size,$mode) = @_;
    my $tmp = "/tmp/mx$$";
    open my $f, '>', $tmp or die $!;
    print $f $in; close $f;
    my $out = `$mock_term $size $mode <$tmp 2>/dev/null`;
    unlink $tmp;
    $out;
}

my %esc = (n=>"\n",t=>"\t",r=>"\r",b=>"\x08",e=>"\x1b",a=>"\a",f=>"\f");
sub de { my $s = shift;
    $s =~ s{\\(?:(\\)|([0-7])([0-7]{0,2})|(.))}{
        defined $1 ? '\\'
        : defined $2 ? ($2 eq '0' && $3 eq '' ? '\\' : chr(oct($2.$3)))
        : (exists $esc{$4} ? $esc{$4} : "\\$4")
    }gex;
    $s;
}
sub dh { my $s = shift; $s =~ s/[^0-9a-fA-F]//g; pack 'H*', $s }

sub mode_for {
    my %f = (F=>'colour-foreground',M=>'mode',C=>'colour',T=>'title');
    for (qw(F M C T)) { my $c = $_; return $f{$c} if grep /^$c /, @_ }
    'cursor-marker';
}

sub run_tfiles {
    for (sort glob(dirname($0).'/*.t')) {
        open my $fh, '<', $_ or next;
        my ($args,$input,@exp,@raw) = ('','');
        my $sec = 'input';
        while (my $line = <$fh>) {
            chomp $line; $line =~ s/\r$//;
            $sec = 'output', next if $line eq '';
            if ($sec eq 'input') {
                if    ($line =~ /^@ (.*)/) { $args = $1 }
                elsif ($line =~ /^< (.*)/) { $input .= de($1) }
                elsif ($line =~ /^\{ (.*)/) { $input .= dh($1) }
            } elsif ($line =~ /^([CFMT>]) (.*)/) {
                push @raw, $line; push @exp, $2;
            }
        }
        close $fh;

        next unless length($input) && @exp;
        my $expected = join "\n", @exp;
        my @t = split ' ', $args;
        my $size = @t && $t[0] =~ /^\d+x\d+$/ ? shift(@t) : '80x20';
        my $mode = @t ? shift(@t) : mode_for(@raw);
        my $out = run_mock($input, $size, $mode);

        $out eq "$expected\n" ? $pass++
            : (warn "FAIL ", basename($_, '.t'), "\n", $fail++);
    }
}

run_tfiles();
print "\n=== results: $pass passed, $fail failed ===\n";
exit($fail ? 1 : 0);
