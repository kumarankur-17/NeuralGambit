n=int(input())
l=[]
for i in range(n):
    l.append(int(input()))
dp=[[0]*n for i in range(n)]

for i in range(n):
    dp[i][i]=l[i]

for len in range(2,n+1):
    for i in range(n-len+1):
        j=i+len-1
        dp[i][j]=max(l[i]-dp[i+1][j],l[j]-dp[i][j-1])

if dp[0][n-1]>0:
    print("Player 1 wins")
elif dp[0][n-1]<0:
    print("Player 2 wins")
else:
    print("Tie")