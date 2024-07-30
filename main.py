#from flask import Flask
from flask import Flask, request, send_file
from gtts import gTTS
import os
# Flask constructor takes the name of
# current module (__name__) as argument.
app = Flask(__name__)

# The route() function of the Flask class is a decorator,
# which tells the application which URL should call
# the associated function.
@app.route('/')
# ‘/’ URL is bound with hello_world() function.
def hello_world():


    language = 'en'
    file = open('speech2text.txt', 'r').read().replace("\n", ' ')
    audio_file = gTTS(text=str(file), lang=language, slow=False)
    audio_file.save("simpleAudio.mp3")
    os.system('start simpleAudio.mp3')

    return 'playing wav file...'
# main driver function
if __name__ == '__main__':

    # run() method of Flask class runs the application
    # on the local development server.
    app.run()
