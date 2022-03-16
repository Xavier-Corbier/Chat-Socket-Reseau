./compile.sh

if  ! lsof -i:$2; then
    gnome-terminal -- bash -c "./serveur $2; bash; sleep 2;" ;

    for (( i=0; i < $1; i++ )); do gnome-terminal -- bash -c "./client 127.0.0.1 $2; bash; sleep 2;"; done
else
    echo "Command failed"
fi



