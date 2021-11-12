%start preproc

dig       [0-9]
hdig      [0-9a-fA-F]
letter    [a-zA-Z]
int       {dig}+
int_hex   0(x|X){hdig}+
id        ({letter}|_)({letter}|{dig}|_)*
real      (({dig}+(\.{dig}*)?)|(\.{dig}+))((e|E)(\+|\-)?{dig}+)?
ws        [ \t\n] | \\(.|\n)
not_eol   [^\n\\] | \\(.|\n)

comment1    \/\* ( [^*\\] | \\(.|\n) | \*+([^*/\\]|\\(.|\n)) )* \*+\/
comment2    \/\/ {not_eol}*

string1     \" ( [^"\\] | \\(.|\n) )* \"
string2     \' ( [^'\\] | \\(.|\n) )* \'

%%

preproc_body    <preproc> {not_eol}*

comment     {comment1}|{comment2}
preproc     #{ws}*{id}
string      {string1}|{string2}
id          {id}
int         {int}|{int_hex}
real        {real}
ws          {ws}+
eof         <<EOF>>
other       .

%%
