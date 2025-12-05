export PROJECT="elysium"
autoload -Uz colors && colors
export PS1="%F{cyan}[elysium:%~]%f %F{yellow}\$(git branch --show-current)%f %# "
