#!/usr/bin/perl
# Worker minimo en Perl: lee una linea, responde una linea (en mayusculas).
# Demuestra que el bridge de TypeEasy funciona con CUALQUIER lenguaje en Linux.
$| = 1;  # autoflush ON (equivalente a flush tras cada respuesta)
while (my $line = <STDIN>) {
    chomp $line;
    print uc($line) . "\n";
}
