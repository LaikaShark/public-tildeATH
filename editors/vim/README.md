# Vim syntax highlighting for ~ATH

Highlighting for `.ath` source files (the `athc` dialect). Covers comments,
the statement keywords, `~ATH` / `.DIE` / `..` / `!` tokens, `import number`
and `import builtin` sub-keywords, the `watch signal|pid|mtime` heads,
`importf <name>` search-path targets, int/bigint/float literals, string
literals with their escapes, the `NULL`/`THIS` constants, and the `print`
payload (literal text with `$NAME` interpolation and `\; \\ \n \t \r \$`
escapes).

## Install

### Native Vim (`~/.vim`)

```sh
mkdir -p ~/.vim/syntax ~/.vim/ftdetect
cp editors/vim/syntax/ath.vim   ~/.vim/syntax/
cp editors/vim/ftdetect/ath.vim ~/.vim/ftdetect/
```

Make sure `syntax on` and `filetype on` are set in your `~/.vimrc`.

### Neovim (`~/.config/nvim`)

```sh
mkdir -p ~/.config/nvim/syntax ~/.config/nvim/ftdetect
cp editors/vim/syntax/ath.vim   ~/.config/nvim/syntax/
cp editors/vim/ftdetect/ath.vim ~/.config/nvim/ftdetect/
```

### Plugin managers

The `editors/vim` directory is a self-contained runtimepath entry, so any
manager that adds a directory to `runtimepath` works. For example, with a
plain `packadd`:

```sh
git clone <this-repo> ~/.vim/pack/athc/start/athc
```

(only `editors/vim/` is needed on the runtimepath).

## Try it

```vim
:set runtimepath^=/path/to/athc/editors/vim
:edit examples/maze.ath
```
