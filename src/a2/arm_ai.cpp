#include "arm_ai.hpp"

arm_ai::arm_ai(int p){
    player = p;
}

int arm_ai::get_player(){
    return player;
}

int arm_ai::min_max(int board[9],int player){ 
    /*int winner = is_win(board);
      if(winner != 0){
      return winner*player;
      }
      int move = -1;
      int score = -2;
      for(int i=0;i<9;++i){
      if(board[i] == 0){
      board[i] = player;
      int thisScore = -min_max(board,player*-1);
      if(thisScore > score){
      score = thisScore;
      move = i;
      }
      board[i] = 0;
      }
      }
      if(move == -1){
      return 0;
      }
      return score;*/
    //How is the position like for player (their turn) on board?
    int winner = is_win(board);
    if(winner != 0) return winner*player;

    int move = -1;
    int score = -2;//Losing moves are preferred to no move
    int i;
    for(i = 0; i < 9; ++i) {//For all moves,
        if(board[i] == 0) {//If legal,
            board[i] = player;//Try the move
            int thisScore = -min_max(board, player*-1);
            if(thisScore > score) {
                score = thisScore;
                move = i;
            }//Pick the one that's worst for the opponent
            board[i] = 0;//Reset board after try
        }
    }
    if(move == -1) return 0;
    return score;
}

int arm_ai::is_win(const int board[9]) {
    //determines if a player has won, returns 0 otherwise.
    unsigned wins[8][3] = {{0,1,2},{3,4,5},{6,7,8},{0,3,6},{1,4,7},{2,5,8},{0,4,8},{2,4,6}};
    int i;
    for(i = 0; i < 8; ++i) {
        if(board[wins[i][0]] != 0 &&
                board[wins[i][0]] == board[wins[i][1]] &&
                board[wins[i][0]] == board[wins[i][2]])
            return board[wins[i][2]];
    }
    return 0;
}

int arm_ai::calc_move(int board[9]){
    /*int move = -1;
      int score = -2;
      for(int i = 0;i<9;++i){
      if(board[i] == 0){
      board[i] = 1;
      int tempScore = -min_max(board,player);
      board[i] = 0;
      if(tempScore > score){
      score = tempScore;
      move = i;
      }
      }
      }
    //board[move] = 1;
    return move;*/
    int move = -1;
    int score = -2;
    int i;
    for(i = 0; i < 9; ++i) {
        if(board[i] == 0) {
            board[i] = 1;
            int tempScore = -min_max(board, -1);
            board[i] = 0;
            if(tempScore > score) {
                score = tempScore;
                move = i;
            }
        }
    }
    //returns a score based on minimax tree at a given node.
    //board[move] = 1;
    return move; 
}
