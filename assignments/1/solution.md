# Blatt1 - Aufgabe 3 - Van-der-Waerden-Zahlen

> Berechnen Sie, wie viele Variablen und Klausen die SAT-Codierung aus der Vorlesung benötigt, um die Eigenschaft W(2, k) > n zu prüfen.

Gibt es eine binäre Zahl (r = 2), deren Länge <= n und die keine Wiederholungen hat, so ist `W(2, k) > n` falsch, andernfalls wahr.
Wir stellen ein SAT Problem auf, das lösbar ist wenn eine solche Zahl existiert und damit  `W(2, k) > n` falsch.

Finden wir eine Zahl mit Wiederholungen der Länge n, z.B. für n = 4 die Zahl `1110` wo sich 1 mit Abstand 1 drei mal wiederholt, 
 so gibt es auch eine Zahl der Länge n + 1, wo sich 1 mit Abstand 1 drei mal wiederholt - wir hängen einfach eine Ziffer an `1110 + 0`.
Finden wir also keine Zahl mit der Länge `n` die keine Wiederholungen enthält, so gibt es auch keine kürzere Zahl.
Es genügt also zu prüfen, ob es keine Zahl mit _exakt der Länge n_ gibt, um `W(2, k) > n` zu prüfen. 

Sei `x(i) = 1` falls an der i-ten Stelle eine 1 in der Zeichenkette steht, sonst 0. Für eine n-stellige Zeichenkette werden also n Variablen benötigt, um die Zeichenkette darzustellen.

Sei `d >= 1` der Abstand zwischen den Wiederholungen. Dann fordern wir `not(x(i) = 1 and x(i + d) = 1 ...)` (1 wiederholt sich mit Abstand d, k mal) und `not(x(i) = 0 and x(i + d) = 0 ...)`.

Für `d = 1, n = 9, k = 3` brauchen wir folgende Klauseln:

```
123456789
^^^         -> 123, -1-2-3   +
 ^^^        -> 234, -2-3-4   |-> n - Länge des Musters + 1 = 9 - 3 + 1 = 7
  ^^^       -> 345, -3-4-5   |
   ^^^      -> 456, -4-5-6   |
    ^^^     -> 567, -5-6-7   |
     ^^^    -> 678, -6-7-8   |
      ^^^   -> 789, -7-8-9   +
      +++ -> Länge des Musters = d * (k - 1) + 1 = 1 * 2 + 1 = 3
```

Für `d = 2, n = 9, k = 3` brauchen wir folgende Klauseln:

```
123456789
^ ^ ^       -> 135, -1-3-5   +
 ^ ^ ^      -> 246, -2-4-6   |-> n - Länge des Musters + 1 = 9 - 5 + 1 = 5
  ^ ^ ^     -> 357, -3-5-7   |
   ^ ^ ^    -> 456, -4-6-8   |
    ^ ^ ^   -> 579, -5-7-9   +
    +++++ -> Länge des Musters = d * (k - 1) + 1 = 2 * 2 + 1 = 5
```

Das Muster um auf `k` Wiederholungen mit Abstand `d` zu prüfen ist damit `d * (k - 1) + 1` lang.
In einer Zeichenkette mit Länge `n` kann dieses Muster an der ersten, zweiten, ... Stelle auftauchen.
Wir verschieben das Muster also jeweils um eine Stelle, ergo taucht das Muster `n - (d * (k - 1) + 1) + 1` mal auf. 

Wir benötigen also `2 * (n - d * (k - 1))` Klauseln, um auf eine Wiederholung mit Abstand `d` zu prüfen. 
Wir wollen allerdings auf alle möglichen Abstände prüfen - welche Abstände kann es geben?

Das Muster muss kürzer oder gleich lang wie die Zeichenkette sein, also `d * (k - 1) + 1 <= n`:

```
d * (k - 1) + 1 <= n | -1
d * (k - 1) <= n - 1 | / (k - 1) - wir fordern k >= 2 (eine einmalige Wiederholung ist trivial)
d <= (n - 1) / (k - 1)
```

Es gibt also folgende Anzahl an Klauseln:

```
sum(d = 1, d <= (n - 1) / (k - 1), 2 * (n - d * (k - 1)))
```

Z.B. für `n = 9, k = 3` (das Beispiel aus den Slides):

```
sum(d = 1, d <= (9 - 1) / (3 - 1), 2 * (9 - d * (3 - 1)))
= sum(d = 1, d <= 4, 2 * (9 - d * 2))
= 2 * (9 - 1 * 2) + 2 * (9 - 2 * 2) + 2 * (9 - 3 * 2) + 2 * (9 - 4 * 2)
= 2 * 7 + 2 * 5 + 2 * 3 + 2 * 1
= 14 + 10 + 6 + 2
= 32
```


# Blatt1 - Aufgabe 4 - Pythagoräische Tripel

> Schätzen Sie die Anzahl der Variablen und Klauseln ab für die SAT-Codierung aus der Vorlesung
für das Pythagoräische-Triple-Problem bis zur Zahl n.

Sei T(n) die Anzahl der möglichen Tripel:

````
T(n) = |{ (a, b, c) | c ∈ (1, n), a^2 + b^2 = c^2 }|
````

Dann ist die Anzahl der Variablen des SAT Problems `n`,
 und die Anzahl der Klauseln `T(n) * 2`, da pro möglichem Tripel zwei Klauseln benötigt werden (z.B. `3 v 4 v 5` und `-3 v -4 v -5`).

Um alle Tripel zu finden iterieren wir über `b` und `c`, und suchen ein `a`:

```cpp
for (int c = 1; c < n; c++) {
    for (int b = 1; b < c; b++) {
        // ...
    }
}
```

Wir wissen das `b < c`, denn falls `b = c` folgt `a = 0`. Die Schleifen iterieren insgesamt
 `n * (n - 1) / 2` mal (Dreieckszahl), jeder Schleifendurchlauf findet maximal ein Tripel.
Also gilt `T(n) < n * (n - 1) / 2`. Eine obere Schranke für die Anzahl an Klauseln ist also `n * (n - 1)`.

----

> Finden Sie mit Hilfe eines SAT Solvers eine Lösung (d.h. eine Färbung) für die ersten 1000 Zahlen (1, 2, ..., 1000) des Pythagoräischen-Triple-Problems.

Für n = 1000 liefert Kissat folgende mögliche Lösung:

```
s SATISFIABLE
v 1 2 -3 -4 5 -6 -7 -8 -9 10 -11 -12 -13 -14 15 -16 -17 -18 -19 20 -21 -22 -23
v 24 -25 -26 -27 -28 -29 -30 -31 -32 -33 34 35 -36 -37 -38 -39 -40 41 -42 -43
v -44 45 -46 47 -48 -49 50 -51 -52 -53 -54 55 -56 -57 58 59 60 -61 -62 -63 -64
v 65 -66 67 -68 -69 70 71 72 -73 -74 -75 -76 -77 -78 79 80 -81 -82 83 -84 85
v 86 -87 -88 -89 -90 -91 -92 -93 94 95 96 -97 -98 -99 -100 -101 102 103 -104
v 105 106 107 -108 -109 110 -111 -112 -113 -114 115 -116 -117 118 -119 -120
v -121 122 123 -124 125 -126 127 -128 -129 130 131 -132 -133 134 135 136 -137
v -138 139 140 -141 142 -143 -144 145 -146 -147 -148 -149 150 151 -152 -153
v -154 155 -156 -157 158 -159 160 -161 -162 163 -164 165 166 167 168 169 -170
v -171 -172 -173 174 -175 176 -177 -178 179 180 -181 -182 -183 -184 185 -186
v -187 -188 -189 190 191 192 -193 -194 195 -196 -197 198 199 -200 -201 -202
v -203 -204 205 206 -207 -208 -209 210 211 -212 -213 214 215 216 -217 218 -219
v -220 221 -222 223 -224 -225 226 227 -228 -229 230 -231 -232 -233 234 235
v -236 -237 -238 239 240 241 242 -243 -244 245 -246 -247 -248 -249 -250 251
v 252 253 254 255 -256 -257 -258 -259 260 -261 262 263 -264 265 -266 -267 -268
v -269 -270 271 -272 -273 -274 275 -276 -277 278 -279 -280 -281 -282 283 -284
v 285 286 287 288 -289 -290 -291 292 -293 -294 295 -296 -297 -298 -299 -300
v -301 302 -303 304 305 306 307 -308 -309 310 311 312 -313 314 -315 -316 317
v -318 319 -320 -321 -322 323 -324 -325 326 -327 -328 329 -330 331 -332 333
v 334 335 -336 -337 -338 -339 340 -341 -342 343 -344 345 346 347 -348 -349 350
v -351 -352 353 -354 355 356 357 358 359 -360 361 362 -363 364 -365 366 367
v -368 369 370 371 -372 -373 -374 375 -376 -377 -378 379 -380 -381 382 383
v -384 385 -386 -387 -388 -389 -390 -391 -392 -393 394 395 396 397 398 -399
v 400 401 -402 -403 -404 405 406 407 408 409 410 411 -412 413 -414 415 416
v -417 -418 419 420 -421 422 -423 424 -425 -426 -427 -428 -429 430 431 -432
v -433 434 435 -436 437 -438 439 440 -441 442 443 -444 -445 446 -447 448 449
v 450 -451 -452 -453 454 455 -456 -457 -458 -459 460 461 -462 463 464 465 -466
v 467 468 469 470 -471 -472 -473 -474 475 -476 -477 478 479 480 -481 -482 483
v -484 485 -486 487 -488 -489 490 491 -492 -493 -494 495 -496 497 -498 499
v -500 -501 502 503 504 505 -506 -507 -508 509 -510 -511 512 -513 514 515 -516
v 517 -518 -519 -520 -521 522 523 -524 -525 526 -527 -528 529 530 -531 532 533
v -534 535 -536 -537 538 -539 540 -541 542 -543 -544 545 -546 547 -548 -549
v 550 551 552 553 -554 555 -556 -557 -558 -559 -560 -561 562 563 -564 565 566
v 567 -568 569 570 571 572 -573 574 -575 576 577 578 -579 -580 581 582 -583
v 584 -585 -586 587 -588 -589 590 591 592 -593 -594 595 596 -597 -598 599 -600
v -601 602 603 -604 605 606 607 608 -609 610 -611 -612 -613 614 -615 -616 -617
v 618 619 620 621 622 623 -624 625 -626 -627 -628 -629 -630 631 -632 633 -634
v 635 -636 -637 638 639 -640 641 642 643 -644 645 -646 647 648 649 650 -651
v -652 -653 654 655 656 -657 658 659 -660 661 662 663 -664 665 -666 -667 -668
v 669 670 671 -672 -673 -674 -675 -676 677 678 -679 680 681 682 683 684 685
v 686 -687 -688 -689 -690 691 692 -693 694 695 -696 697 698 699 700 -701 -702
v -703 704 705 -706 -707 -708 709 710 711 -712 -713 714 715 -716 717 718 719
v -720 721 722 723 724 725 726 727 728 729 -730 -731 732 -733 734 735 -736 737
v 738 739 -740 -741 742 743 744 -745 -746 747 748 749 750 751 -752 753 754 755
v -756 -757 758 759 -760 761 762 -763 -764 765 766 -767 -768 769 770 -771 772
v -773 774 -775 -776 -777 -778 779 780 781 782 -783 -784 785 786 787 -788 789
v 790 -791 -792 -793 -794 795 -796 -797 -798 -799 800 801 -802 -803 -804 -805
v -806 -807 -808 -809 -810 811 812 813 -814 815 816 817 -818 819 -820 -821 822
v 823 824 825 826 827 -828 -829 830 -831 832 -833 834 835 -836 -837 838 839
v -840 841 -842 -843 844 845 846 -847 -848 849 -850 851 -852 -853 854 -855 856
v -857 858 859 860 861 862 863 -864 -865 -866 -867 868 869 870 -871 -872 -873
v -874 875 876 877 878 879 880 -881 -882 883 -884 885 886 887 888 889 890 -891
v 892 893 -894 895 -896 -897 898 -899 900 -901 -902 903 904 -905 906 907 908
v -909 910 911 -912 913 914 -915 -916 917 918 919 -920 921 -922 -923 924 -925
v 926 927 928 929 -930 931 -932 933 934 -935 936 -937 938 -939 940 941 942 943
v 944 -945 946 947 948 -949 950 -951 -952 -953 -954 955 956 957 958 -959 960
v 961 -962 963 -964 965 -966 967 968 969 970 971 972 973 974 -975 -976 977 978
v -979 980 -981 982 983 -984 985 -986 987 988 989 990 991 992 993 994 995 996
v 997 998 999 1000 0
```