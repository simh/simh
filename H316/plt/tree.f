      DOUBLE PRECISION X, Y

C     INITIALIZE THE PLOTTER      
      CALL ORIGIN
      X = 0.0D0
      Y = 0.0D0
      
C     MOVE AWAY FROM THE EDGE OF THE PAPER
      CALL MOVE(84.0, 0.0, X, Y)

C     DRAW A PYTHAGOREAN CHRISTMAS TREE
      CALL DRAW(0.0D0, 16.0D0, X, Y)
      CALL TREE(0.25D0, 12.0D0, 30, X, Y)
      CALL DRAW(0.0D0, -Y, X, Y)

C     PUT A STAR ON IT
      CALL MOVE(81.75, 129.0, X, Y)
      CALL SNOW(0.0D0, 16.0D0, 2)

C     ADD SOME SNOW FLAKES
      CALL MOVE(30.0, 55.0, X, Y)
      CALL SNOW(0.00D0, 10.0D0, 4)
      
      CALL MOVE(42.0, 86.0, X, Y)
      CALL SNOW(0.05D0, 18.0D0, 4)
      
      CALL MOVE(110.0, 10.0, X, Y)
      CALL SNOW(0.10D0, 12.0D0, 4)

      CALL MOVE(115.0, 100.0, X, Y)
      CALL SNOW(0.15D0, 16.0D0, 4)

      CALL MOVE(130.0, 70.0, X, Y)
      CALL SNOW(0.20D0, 10.0D0, 4)

C     SEASON'S GREETINGS
      CALL SYMBOL(39.5, -12.0, 8.0, 15HMERRY CHRISTMAS, 0.0, 15)

C     FINAL MOVE WITH PEN UP
      CALL MOVE(0.0, 0.0, X, Y)
      STOP
      END

      SUBROUTINE TREE(TI, LI, D, X, Y)
      DOUBLE PRECISION TI, LI, X, Y
      INTEGER D
      DOUBLE PRECISION PI, DEG30, DEG60, COS30
      DOUBLE PRECISION T(30), L(30), DX(30), DY(30)
      INTEGER S(30), J
      LOGICAL EVEN(30)
      
      PI=4.0D0*DATAN(1.0D0)
      DEG30 = PI/6.0D0
      DEG60 = PI/3.0D0
      COS30 = DCOS(DEG30)

      DO 1 I = 1, D, 2
      EVEN(I)   = .FALSE.
      IF (I.LT.D) EVEN(I+1) = .TRUE.
 1    CONTINUE
      
      I = 1
      T(I) = TI
      L(I) = LI
      S(I) = 1
      
 5    J=S(I)
      GO TO (10, 20, 30, 40), J
      
 10   S(I)=2
      DX(I) =-L(I)*DSIN(T(I))
      DY(I) = L(I)*DCOS(T(I))
      CALL DRAW(DX(I), DY(I), X, Y)
      GO TO 50
      
 20   S(I) = 3
      IF ((I .GE .D) .OR. (L(I) .LT. 0.2D0)) GO TO 22
      IF (EVEN(I)) GO TO 21
      T(I+1) = T(I)+DEG60
      L(I+1) = 0.5 * L(I)
      I = I + 1
      S(I) = 1
      GO TO 50
 21   T(I+1) = T(I)+DEG30
      L(I+1) = COS30 * L(I)
      I = I + 1
      S(I) = 1
      GO TO 50
 22   CALL DRAW(DY(I),-DX(I), X, Y)
      S(I) = 4
      GO TO 50
      
 30   S(I) = 4
      IF (EVEN(I)) GO TO 31
      T(I+1) = T(I) - DEG30
      L(I+1) = COS30 * L(I)
      I = I + 1
      S(I) = 1
      GOTO 50
 31   T(I+1) = T(I) - DEG60
      L(I+1) = 0.5 * L(I)
      I = I + 1
      S(I) = 1
      GOTO 50
      
 40   CALL DRAW(-DX(I),-DY(I), X, Y)
      I = I-1

 50   IF (I.GT.0) GO TO 5
      RETURN
      END

      SUBROUTINE SNOW(TI, LI, D)
      DOUBLE PRECISION TI, LI
      INTEGER D
      DOUBLE PRECISION PI, DEG60
      
      PI=4.0D0*DATAN(1.0D0)
      DEG60 = PI/3.0D0

      CALL KOCH(TI+DEG60, LI, D)
      CALL KOCH(TI-DEG60, LI, D)
      CALL KOCH(TI+PI,    LI, D)
      RETURN
      END
      
      SUBROUTINE KOCH(TI, LI, D)
      DOUBLE PRECISION TI, LI
      INTEGER D
      DOUBLE PRECISION PI, DEG60
      DOUBLE PRECISION T(20), L(20)
      INTEGER S(20), I, J
      
      PI=4.0D0*DATAN(1.0D0)
      DEG60 = PI/3.0D0

      I = 1
      T(I) = TI
      L(I) = LI
      S(I) = 1

 5    IF (I.GE.D) GO TO 70
      J = S(I)
      GO TO (10, 20, 30, 40), J
      
 10   T(I+1) = T(I)
      GO TO 50

 20   T(I+1) = T(I) + DEG60
      GO TO 50

 30   T(I+1) = T(I) - DEG60
      GO TO 50

 40   T(I+1) = T(I)

 50   IF (J.GT.4) GO TO 55
      S(I) = S(I) + 1
      L(I+1) = L(I) / 3.0D0
      I = I+1
      S(I) = 1
      GO TO 60
 55   I = I - 1
 60   GO TO 80

 70   CALL DRAW(L(I) * DCOS(T(I)), L(I) * DSIN(T(I)))
      I = I-1

 80   IF (I.GT.0) GO TO 5
      RETURN
      END
      
      SUBROUTINE DRAW(DX, DY, X, Y)
      DOUBLE PRECISION DX, DY, X, Y
      REAL XX, YY
      X = X + DX
      Y = Y + DY
      XX = X
      YY = Y
      CALL PLOT(XX, YY, 2)      
      RETURN
      END
      
      SUBROUTINE MOVE(XX, YY, X, Y)
      REAL XX, YY
      DOUBLE PRECISION X, Y
      X = XX
      Y = YY
      CALL PLOT(XX, YY, 3)
      RETURN
      END

$0
