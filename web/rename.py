# shuffle file and rename 
import os 
from random import shuffle
# Function to rename multiple files 
def main(): 
    
    num = []
    for count in range(200):
        if count < 10:
            dst = "00" + str(count) + ".jpg"
            num.append(dst)
        elif count < 100 and count > 9:
            dst = "0" + str(count) + ".jpg"
            num.append(dst)
        else:
            dst = str(count) + ".jpg"
            num.append(dst)
    shuffle(num)       
    for count, filename in enumerate(os.listdir("./data/")): 
        src ='./data/'+ filename 
        dst ='./data/'+ num[count] 
          
        # rename() function will 
        # rename all the files 
        os.rename(src, dst) 
  
# Driver Code 
if __name__ == '__main__': 
      
    # Calling main() function 
    main() 